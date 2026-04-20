#include <benchmark/benchmark.h>
#include "velox/matching/order.hpp"
#include "lockfree/pool.hpp"

using namespace velox;
using namespace lockfree;
using namespace benchmark;

static void BM_OrderAcquireFill(State& state) {
    ObjectPool<Order, 100000> pool;
    
    for (auto _ : state) {
        auto order = pool.acquire();
        order->order_id = 123;
        order->quantity = 100;
        order->remaining_quantity = 100;
        order->fill(50);
        DoNotOptimize(order);
    }
}
BENCHMARK(BM_OrderAcquireFill);

static void BM_OrderCreateOnly(State& state) {
    ObjectPool<Order, 100000> pool;
    
    for (auto _ : state) {
        auto order = pool.acquire();
        DoNotOptimize(order);
    }
}
BENCHMARK(BM_OrderCreateOnly);

static void BM_OrderFillOnly(State& state) {
    ObjectPool<Order, 100000> pool;
    auto order = pool.acquire();
    order->quantity = 100;
    order->remaining_quantity = 100;
    
    for (auto _ : state) {
        order->fill(1);
        DoNotOptimize(order);
    }
}
BENCHMARK(BM_OrderFillOnly);

BENCHMARK_MAIN();