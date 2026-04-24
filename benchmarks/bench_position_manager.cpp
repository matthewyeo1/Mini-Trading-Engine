#include <benchmark/benchmark.h>
#include "velox/risk/position_manager.hpp"
#include "lockfree/pool.hpp"
#include <cstring>

using namespace velox;

class PositionManagerBench {
public:
    PositionManagerBench() : pool(std::make_unique<lockfree::ObjectPool<Order, 100000>>()) {
        mgr = std::make_unique<PositionManager>();
    }

    Order* create_order(uint64_t id, OrderSide side, const char* symbol, int64_t price, uint32_t qty) {
        auto order = pool->acquire();
        order->order_id = id;
        order->side = side;
        order->price = price;
        order->quantity = qty;
        order->remaining_quantity = qty;
        order->filled_quantity = 0;
        std::strncpy(order->symbol, symbol, 7);
        Order* raw = order.get();
        owned.push_back(std::move(order));
        return raw;
    }

    std::unique_ptr<lockfree::ObjectPool<Order, 100000>> pool;
    std::unique_ptr<PositionManager> mgr;
    std::vector<lockfree::PooledPtr<Order, 100000>> owned;
};

static void BM_PositionManager_UpdateBuy(benchmark::State& state) {
    PositionManagerBench bench;
    Order* order = bench.create_order(1, OrderSide::BUY, "AAPL", 10000, 100);

    for (auto _ : state) {
        bench.mgr->update_position(order, 100, 10000);
        benchmark::DoNotOptimize(bench.mgr);
    }
}
BENCHMARK(BM_PositionManager_UpdateBuy);

static void BM_PositionManager_UpdateSell(benchmark::State& state) {
    PositionManagerBench bench;
    // First create a buy to have a position
    Order* buy = bench.create_order(1, OrderSide::BUY, "AAPL", 10000, 100);
    bench.mgr->update_position(buy, 100, 10000);

    Order* sell = bench.create_order(2, OrderSide::SELL, "AAPL", 10100, 100);

    for (auto _ : state) {
        bench.mgr->update_position(sell, 100, 10100);
        benchmark::DoNotOptimize(bench.mgr);
    }
}
BENCHMARK(BM_PositionManager_UpdateSell);

static void BM_PositionManager_GetPosition(benchmark::State& state) {
    PositionManagerBench bench;
    Order* buy = bench.create_order(1, OrderSide::BUY, "AAPL", 10000, 100);
    bench.mgr->update_position(buy, 100, 10000);

    for (auto _ : state) {
        int64_t pos = bench.mgr->get_position("AAPL");
        benchmark::DoNotOptimize(pos);
    }
}
BENCHMARK(BM_PositionManager_GetPosition);

static void BM_PositionManager_GetRealizedPnL(benchmark::State& state) {
    PositionManagerBench bench;
    Order* buy = bench.create_order(1, OrderSide::BUY, "AAPL", 10000, 100);
    bench.mgr->update_position(buy, 100, 10000);
    Order* sell = bench.create_order(2, OrderSide::SELL, "AAPL", 10100, 100);
    bench.mgr->update_position(sell, 100, 10100);

    for (auto _ : state) {
        int64_t pnl = bench.mgr->get_realized_pnl("AAPL");
        benchmark::DoNotOptimize(pnl);
    }
}
BENCHMARK(BM_PositionManager_GetRealizedPnL);

static void BM_PositionManager_GetUnrealizedPnL(benchmark::State& state) {
    PositionManagerBench bench;
    Order* buy = bench.create_order(1, OrderSide::BUY, "AAPL", 10000, 100);
    bench.mgr->update_position(buy, 100, 10000);
    // No sell; position still open

    for (auto _ : state) {
        int64_t pnl = bench.mgr->get_unrealized_pnl("AAPL", 10100);
        benchmark::DoNotOptimize(pnl);
    }
}
BENCHMARK(BM_PositionManager_GetUnrealizedPnL);

static void BM_PositionManager_Mixed(benchmark::State& state) {
    // Create reusable objects once outside the loop
    lockfree::ObjectPool<Order, 100000> pool;
    PositionManager mgr;
    std::vector<lockfree::PooledPtr<Order, 100000>> owned;

    // Helper to create an order (only once)
    auto create_order = [&](uint64_t id, OrderSide side, const char* sym, int64_t price, uint32_t qty) {
        auto h = pool.acquire();
        if (!h) state.SkipWithError("Pool exhausted");
        h->order_id = id;
        h->side = side;
        h->price = price;
        h->quantity = qty;
        h->remaining_quantity = qty;
        h->filled_quantity = 0;
        std::strncpy(h->symbol, sym, 7);
        owned.push_back(std::move(h));
        return owned.back().get();
    };

    // Create all orders once (they will be reused)
    Order* buy1 = create_order(1, OrderSide::BUY, "AAPL", 10000, 100);
    Order* buy2 = create_order(2, OrderSide::BUY, "AAPL", 10200, 50);
    Order* sell1 = create_order(3, OrderSide::SELL, "AAPL", 10100, 80);
    Order* sell2 = create_order(4, OrderSide::SELL, "AAPL", 10300, 70);

    for (auto _ : state) {
        // Reset the PositionManager state (clear all positions)
        mgr.reset();

        // Re‑initialize orders (reset their fill state)
        buy1->remaining_quantity = 100; buy1->filled_quantity = 0;
        buy2->remaining_quantity = 50;  buy2->filled_quantity = 0;
        sell1->remaining_quantity = 80; sell1->filled_quantity = 0;
        sell2->remaining_quantity = 70; sell2->filled_quantity = 0;

        // Simulate the sequence
        mgr.update_position(buy1, 100, 10000);
        mgr.update_position(buy2, 50, 10200);
        mgr.update_position(sell1, 80, 10100);
        mgr.update_position(sell2, 70, 10300);

        // Query P&L
        int64_t realized = mgr.get_realized_pnl("AAPL");
        int64_t unrealized = mgr.get_unrealized_pnl("AAPL", 10150);
        benchmark::DoNotOptimize(realized);
        benchmark::DoNotOptimize(unrealized);
    }
}
BENCHMARK(BM_PositionManager_Mixed);

static void BM_PositionManager_Throughput(benchmark::State& state) {
    lockfree::ObjectPool<Order, 100000> pool;
    PositionManager mgr;
    std::vector<lockfree::PooledPtr<Order, 100000>> owned;

    auto create_order = [&](uint64_t id, OrderSide side, const char* sym, int64_t price, uint32_t qty) {
        auto h = pool.acquire();
        if (!h) state.SkipWithError("Pool exhausted");
        h->order_id = id;
        h->side = side;
        h->price = price;
        h->quantity = qty;
        h->remaining_quantity = qty;
        h->filled_quantity = 0;
        std::strncpy(h->symbol, sym, 7);
        owned.push_back(std::move(h));
        return owned.back().get();
    };

    Order* order = create_order(1, OrderSide::BUY, "AAPL", 10000, 100);

    for (auto _ : state) {
        mgr.reset();                         // Clear positions
        order->remaining_quantity = 100;
        order->filled_quantity = 0;
        mgr.update_position(order, 100, 10000);
        benchmark::DoNotOptimize(mgr);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_PositionManager_Throughput);

BENCHMARK_MAIN();