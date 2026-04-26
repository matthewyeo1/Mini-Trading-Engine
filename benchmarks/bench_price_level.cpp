#include <benchmark/benchmark.h>
#include "velox/book/price_level.hpp"
#include "lockfree/pool.hpp"

using namespace velox;
using namespace benchmark;
using namespace lockfree;

static void BM_PriceLevel_AddOrder(State& state) {
    ObjectPool<Order, 100000> pool;
    PriceLevel level(10000);
    
    for (auto _ : state) {
        auto order = pool.acquire();
        order->order_id = 1;
        order->quantity = 100;
        order->remaining_quantity = 100;
        level.add_order(order.get());
        DoNotOptimize(level);
    }
}
BENCHMARK(BM_PriceLevel_AddOrder);

static void BM_PriceLevel_RemoveOrder(State& state) {
    ObjectPool<Order, 100000> pool;
    PriceLevel level(10000);
    
    for (auto _ : state) {
        auto order = pool.acquire();
        order->order_id = 1;
        order->quantity = 100;
        order->remaining_quantity = 100;
        level.add_order(order.get());
        level.remove_order(order.get());
        DoNotOptimize(level);
    }
}
BENCHMARK(BM_PriceLevel_RemoveOrder);

static void BM_PriceLevel_MatchOrder(State& state) {
    ObjectPool<Order, 100000> pool;
    PriceLevel level(10000);

    std::vector<Fill> fills;
    fills.reserve(100);
    
    // Setup: add a buy order
    auto buy = pool.acquire();
    buy->order_id = 1;
    buy->side = OrderSide::BUY;
    buy->quantity = 100;
    buy->remaining_quantity = 100;
    level.add_order(buy.get());
    
    for (auto _ : state) {
        auto sell = pool.acquire();
        sell->order_id = 2;
        sell->side = OrderSide::SELL;
        sell->quantity = 60;
        sell->remaining_quantity = 60;
        
        fills.clear();
        auto remaining = level.match_order(sell.get(), fills);
        DoNotOptimize(remaining);
        
        // Restore the buy order for next iteration
        buy->remaining_quantity = 100;
        buy->filled_quantity = 0;
    }
}
BENCHMARK(BM_PriceLevel_MatchOrder);

BENCHMARK_MAIN();