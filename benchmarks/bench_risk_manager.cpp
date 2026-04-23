#include <benchmark/benchmark.h>
#include "velox/risk/risk_manager.hpp"
#include "lockfree/pool.hpp"
#include <cstring>

using namespace velox;

class RiskManagerBench {
public:
    RiskManagerBench() : pool(std::make_unique<lockfree::ObjectPool<Order, 100000>>()) {
        risk = std::make_unique<RiskManager>();
        risk->set_position_limit("AAPL", 10000);
        risk->set_position_limit("MSFT", 5000);
        risk->set_position_limit("GOOGL", 2000);
    }
    
    Order* create_order(uint64_t id, OrderSide side, const char* symbol, int64_t price, uint32_t qty) {
        auto order = pool->acquire();
        order->order_id = id;
        order->client_order_id = id;
        order->side = side;
        order->price = price;
        order->quantity = qty;
        order->remaining_quantity = qty;
        order->filled_quantity = 0;
        order->status = OrderStatus::NEW;
        std::strncpy(order->symbol, symbol, 7);
        Order* raw = order.get();
        owned_orders.push_back(std::move(order));
        return raw;
    }
    
    std::unique_ptr<lockfree::ObjectPool<Order, 100000>> pool;
    std::unique_ptr<RiskManager> risk;
    std::vector<lockfree::PooledPtr<Order, 100000>> owned_orders;
};

static void BM_RiskManager_CheckOrder_Valid(benchmark::State& state) {
    RiskManagerBench bench;
    Order* order = bench.create_order(1, OrderSide::BUY, "AAPL", 10000, 100);
    
    for (auto _ : state) {
        bool result = bench.risk->check_order(order);
        benchmark::DoNotOptimize(result);
    }
}
BENCHMARK(BM_RiskManager_CheckOrder_Valid);

static void BM_RiskManager_CheckOrder_InvalidQuantity(benchmark::State& state) {
    RiskManagerBench bench;
    Order* order = bench.create_order(1, OrderSide::BUY, "AAPL", 10000, 0);
    
    for (auto _ : state) {
        bool result = bench.risk->check_order(order);
        benchmark::DoNotOptimize(result);
    }
}
BENCHMARK(BM_RiskManager_CheckOrder_InvalidQuantity);

static void BM_RiskManager_CheckOrder_NegativePrice(benchmark::State& state) {
    RiskManagerBench bench;
    Order* order = bench.create_order(1, OrderSide::BUY, "AAPL", -1000, 100);
    
    for (auto _ : state) {
        bool result = bench.risk->check_order(order);
        benchmark::DoNotOptimize(result);
    }
}
BENCHMARK(BM_RiskManager_CheckOrder_NegativePrice);

static void BM_RiskManager_CheckPosition_Buy(benchmark::State& state) {
    RiskManagerBench bench;
    Order* order = bench.create_order(1, OrderSide::BUY, "AAPL", 10000, 100);
    
    for (auto _ : state) {
        bool result = bench.risk->check_position(order, 100);
        benchmark::DoNotOptimize(result);
    }
}
BENCHMARK(BM_RiskManager_CheckPosition_Buy);

static void BM_RiskManager_CheckPosition_Sell(benchmark::State& state) {
    RiskManagerBench bench;
    Order* order = bench.create_order(1, OrderSide::SELL, "AAPL", 10000, 100);
    
    for (auto _ : state) {
        bool result = bench.risk->check_position(order, 100);
        benchmark::DoNotOptimize(result);
    }
}
BENCHMARK(BM_RiskManager_CheckPosition_Sell);

static void BM_RiskManager_CheckPosition_MultipleSymbols(benchmark::State& state) {
    RiskManagerBench bench;
    const char* symbols[] = {"AAPL", "MSFT", "GOOGL", "AMZN", "META"};
    
    for (auto _ : state) {
        // Create orders fresh each iteration
        std::vector<lockfree::PooledPtr<Order, 100000>> local_orders;
        local_orders.reserve(5);

        for (int i = 0; i < 5; ++i) {
            auto order = bench.pool->acquire();
            order->order_id = i;
            order->side = OrderSide::BUY;
            order->price = 10000;
            order->quantity = 100;
            order->remaining_quantity = 100;
            std::strncpy(order->symbol, symbols[i], 7);
            local_orders.push_back(std::move(order));
            
            bool result = bench.risk->check_position(local_orders.back().get(), 100);
            benchmark::DoNotOptimize(result);
        }
    }
}
BENCHMARK(BM_RiskManager_CheckPosition_MultipleSymbols);

static void BM_RiskManager_CircuitBreaker_Inactive(benchmark::State& state) {
    RiskManagerBench bench;
    Order* order = bench.create_order(1, OrderSide::BUY, "AAPL", 10000, 100);
    
    for (auto _ : state) {
        bool result = bench.risk->check_order(order);
        benchmark::DoNotOptimize(result);
    }
}
BENCHMARK(BM_RiskManager_CircuitBreaker_Inactive);

static void BM_RiskManager_CircuitBreaker_Active(benchmark::State& state) {
    RiskManagerBench bench;
    Order* order = bench.create_order(1, OrderSide::BUY, "AAPL", 10000, 100);
    bench.risk->activate_circuit_breaker();
    
    for (auto _ : state) {
        bool result = bench.risk->check_order(order);
        benchmark::DoNotOptimize(result);
    }
}
BENCHMARK(BM_RiskManager_CircuitBreaker_Active);

static void BM_RiskManager_SetPositionLimit(benchmark::State& state) {
    RiskManagerBench bench;
    
    for (auto _ : state) {
        bench.risk->set_position_limit("AAPL", 10000);
        benchmark::DoNotOptimize(bench.risk);
    }
}
BENCHMARK(BM_RiskManager_SetPositionLimit);

static void BM_RiskManager_CurrentPosition(benchmark::State& state) {
    RiskManagerBench bench;
    bench.risk->set_position_limit("AAPL", 10000);
    
    for (auto _ : state) {
        uint32_t pos = bench.risk->current_position("AAPL");
        benchmark::DoNotOptimize(pos);
    }
}
BENCHMARK(BM_RiskManager_CurrentPosition);

static void BM_RiskManager_HashSymbol(benchmark::State& state) {
    RiskManagerBench bench;
    const char* symbols[] = {"AAPL", "MSFT", "GOOGL", "AMZN", "META", "TSLA", "NVDA", "JPM", "V", "WMT"};
    
    for (auto _ : state) {
        for (int i = 0; i < 10; ++i) {
            uint32_t hash = bench.risk->hash_symbol(symbols[i]);
            benchmark::DoNotOptimize(hash);
        }
    }
}
BENCHMARK(BM_RiskManager_HashSymbol);

static void BM_RiskManager_Throughput(benchmark::State& state) {
    for (auto _ : state) {
        // Create fresh RiskManager and orders each iteration
        RiskManager risk;
        risk.set_position_limit("AAPL", 100000);
        
        lockfree::ObjectPool<Order, 100000> pool;
        std::vector<lockfree::PooledPtr<Order, 100000>> orders;
        orders.reserve(300);
        
        // Simulate a mix of operations
        for (int i = 0; i < 100; ++i) {
            // Check buy order
            auto buy = pool.acquire();
            buy->order_id = i;
            buy->side = OrderSide::BUY;
            buy->price = 10000 + i;
            buy->quantity = 100;
            buy->remaining_quantity = 100;
            buy->status = OrderStatus::NEW;
            std::strncpy(buy->symbol, "AAPL", 7);
            bool result1 = risk.check_order(buy.get());
            benchmark::DoNotOptimize(result1);
            orders.push_back(std::move(buy));
            
            // Check sell order
            auto sell = pool.acquire();
            sell->order_id = i + 1000;
            sell->side = OrderSide::SELL;
            sell->price = 9900 + i;
            sell->quantity = 100;
            sell->remaining_quantity = 100;
            std::strncpy(sell->symbol, "AAPL", 7);
            bool result2 = risk.check_order(sell.get());
            benchmark::DoNotOptimize(result2);
            orders.push_back(std::move(sell));
            
            // Check position
            auto pos_order = pool.acquire();
            pos_order->order_id = i + 2000;
            pos_order->side = OrderSide::BUY;
            pos_order->price = 10000;
            pos_order->quantity = 100;
            pos_order->remaining_quantity = 100;
            std::strncpy(pos_order->symbol, "AAPL", 7);
            bool result3 = risk.check_position(pos_order.get(), 100);
            benchmark::DoNotOptimize(result3);
            orders.push_back(std::move(pos_order));
        }
        
        // orders destructor releases all pooled pointers
    }
    state.SetItemsProcessed(state.iterations() * 300);
}
BENCHMARK(BM_RiskManager_Throughput);

BENCHMARK_MAIN();