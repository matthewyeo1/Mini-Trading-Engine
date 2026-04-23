#include <benchmark/benchmark.h>
#include "velox/gateway/execution_gateway.hpp"
#include <cstring>
#include <memory>

using namespace velox;

// ============================================================================
// Gateway Benchmarks (Pool created once, reused across iterations)
// ============================================================================

// Create a shared pool for all benchmarks
static ExecutionGateway::ReportPool* g_pool = nullptr;

static void setup_pool() {
    if (!g_pool) {
        g_pool = new ExecutionGateway::ReportPool();
    }
}

static void cleanup_pool() {
    delete g_pool;
    g_pool = nullptr;
}

// Benchmark: Add Worker (Gateway created each iteration, but pool is shared)
static void BM_Gateway_AddWorker(benchmark::State& state) {
    setup_pool();
    for (auto _ : state) {
        ExecutionGateway gateway(g_pool);
        gateway.add_worker();
        benchmark::DoNotOptimize(gateway);
    }
    cleanup_pool();
}
BENCHMARK(BM_Gateway_AddWorker);

// Benchmark: Send Report (Single Worker)
static void BM_Gateway_SendReport(benchmark::State& state) {
    setup_pool();
    ExecutionGateway gateway(g_pool);
    gateway.add_worker();
    gateway.add_worker();
    gateway.add_worker();
    gateway.add_worker();
    
    ExecutionReport report;
    report.order_id = 1;
    report.client_order_id = 100;
    report.side = OrderSide::BUY;
    report.status = OrderStatus::FILLED;
    report.executed_quantity = 100;
    report.executed_price = 10000;
    report.timestamp = 123456789;
    std::strncpy(report.symbol, "AAPL", 7);
    report.reject_reason = nullptr;
    
    for (auto _ : state) {
        bool result = gateway.send_report(report, 0);
        benchmark::DoNotOptimize(result);
    }
    cleanup_pool();
}
BENCHMARK(BM_Gateway_SendReport);

// Benchmark: Send Report (Round-Robin)
static void BM_Gateway_SendReport_RoundRobin(benchmark::State& state) {
    setup_pool();
    ExecutionGateway gateway(g_pool);
    gateway.add_worker();
    gateway.add_worker();
    gateway.add_worker();
    gateway.add_worker();
    
    ExecutionReport report;
    report.order_id = 1;
    report.client_order_id = 100;
    report.side = OrderSide::BUY;
    report.status = OrderStatus::FILLED;
    report.executed_quantity = 100;
    report.executed_price = 10000;
    report.timestamp = 123456789;
    std::strncpy(report.symbol, "AAPL", 7);
    
    for (auto _ : state) {
        bool result = gateway.send_report(report);  // -1 = round-robin
        benchmark::DoNotOptimize(result);
    }
    cleanup_pool();
}
BENCHMARK(BM_Gateway_SendReport_RoundRobin);

// Benchmark: Receive Report
static void BM_Gateway_ReceiveReport(benchmark::State& state) {
    setup_pool();
    ExecutionGateway gateway(g_pool);
    gateway.add_worker();
    gateway.add_worker();
    gateway.add_worker();
    gateway.add_worker();
    
    ExecutionReport report;
    report.order_id = 1;
    report.client_order_id = 100;
    report.side = OrderSide::BUY;
    report.status = OrderStatus::FILLED;
    report.executed_quantity = 100;
    report.executed_price = 10000;
    report.timestamp = 123456789;
    std::strncpy(report.symbol, "AAPL", 7);
    
    gateway.send_report(report, 0);
    
    for (auto _ : state) {
        ExecutionReport* received = gateway.receive_report(0);
        benchmark::DoNotOptimize(received);
    }
    cleanup_pool();
}
BENCHMARK(BM_Gateway_ReceiveReport);

// Benchmark: Send Order (Converts to Report)
static void BM_Gateway_SendOrder(benchmark::State& state) {
    setup_pool();
    ExecutionGateway gateway(g_pool);
    gateway.add_worker();
    gateway.add_worker();
    gateway.add_worker();
    gateway.add_worker();
    
    Order order;
    order.order_id = 1;
    order.client_order_id = 100;
    order.side = OrderSide::BUY;
    order.filled_quantity = 100;
    order.price = 10000;
    order.status = OrderStatus::FILLED;
    order.matched_timestamp = 123456789;
    std::strncpy(order.symbol, "AAPL", 7);
    
    for (auto _ : state) {
        bool result = gateway.send_order(&order);
        benchmark::DoNotOptimize(result);
    }
    cleanup_pool();
}
BENCHMARK(BM_Gateway_SendOrder);

// Benchmark: Cancel Order
static void BM_Gateway_CancelOrder(benchmark::State& state) {
    setup_pool();
    ExecutionGateway gateway(g_pool);
    
    for (auto _ : state) {
        bool result = gateway.cancel_order(12345);
        benchmark::DoNotOptimize(result);
    }
    cleanup_pool();
}
BENCHMARK(BM_Gateway_CancelOrder);

// Benchmark: Multiple Reports to Different Workers
static void BM_Gateway_MultipleReports(benchmark::State& state) {
    setup_pool();
    ExecutionGateway gateway(g_pool);
    gateway.add_worker();
    gateway.add_worker();
    gateway.add_worker();
    gateway.add_worker();
    
    for (auto _ : state) {
        for (int worker = 0; worker < 4; ++worker) {
            ExecutionReport report;
            report.order_id = worker;
            report.client_order_id = worker;
            report.side = OrderSide::BUY;
            report.status = OrderStatus::FILLED;
            report.executed_quantity = 100;
            report.executed_price = 10000;
            report.timestamp = 123456789;
            std::strncpy(report.symbol, "AAPL", 7);
            
            bool result = gateway.send_report(report, worker);
            benchmark::DoNotOptimize(result);
        }
    }
    state.SetItemsProcessed(state.iterations() * 4);
    cleanup_pool();
}
BENCHMARK(BM_Gateway_MultipleReports);

// Benchmark: Pipeline (Send 100 reports, then receive all)
static void BM_Gateway_Pipeline(benchmark::State& state) {
    setup_pool();
    ExecutionGateway gateway(g_pool);
    gateway.add_worker();
    gateway.add_worker();
    gateway.add_worker();
    gateway.add_worker();
    
    for (auto _ : state) {
        // Send 100 reports
        for (int i = 0; i < 100; ++i) {
            ExecutionReport report;
            report.order_id = i;
            report.client_order_id = i;
            report.side = OrderSide::BUY;
            report.status = OrderStatus::FILLED;
            report.executed_quantity = 100;
            report.executed_price = 10000;
            report.timestamp = 123456789;
            std::strncpy(report.symbol, "AAPL", 7);
            gateway.send_report(report, i % 4);
        }
        
        // Receive all reports
        int received = 0;
        for (int worker = 0; worker < 4; ++worker) {
            ExecutionReport* r;
            while ((r = gateway.receive_report(worker)) != nullptr) {
                benchmark::DoNotOptimize(r);
                received++;
            }
        }
    }
    state.SetItemsProcessed(state.iterations() * 100);
    cleanup_pool();
}
BENCHMARK(BM_Gateway_Pipeline);

// Benchmark: Contended Worker (All reports to worker 0)
static void BM_Gateway_ContendedWorker(benchmark::State& state) {
    setup_pool();
    ExecutionGateway gateway(g_pool);
    gateway.add_worker();
    gateway.add_worker();
    gateway.add_worker();
    gateway.add_worker();
    
    for (auto _ : state) {
        for (int i = 0; i < 100; ++i) {
            ExecutionReport report;
            report.order_id = i;
            report.client_order_id = i;
            report.side = OrderSide::BUY;
            report.status = OrderStatus::FILLED;
            report.executed_quantity = 100;
            report.executed_price = 10000;
            report.timestamp = 123456789;
            std::strncpy(report.symbol, "AAPL", 7);
            gateway.send_report(report, 0);  // Always worker 0
        }
    }
    state.SetItemsProcessed(state.iterations() * 100);
    cleanup_pool();
}
BENCHMARK(BM_Gateway_ContendedWorker);

BENCHMARK_MAIN();