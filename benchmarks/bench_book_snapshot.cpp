#include <benchmark/benchmark.h>
#include <thread>
#include <vector>
#include "velox/book/order_book.hpp"
#include "velox/book/book_snapshot.hpp"
#include "lockfree/pool.hpp"
#include <chrono>

using namespace velox;

class BookSnapshotBench {
public:
    BookSnapshotBench() : pool(std::make_unique<lockfree::ObjectPool<Order, 100000>>()) {
        book = std::make_unique<OrderBook>("AAPL");
        // Add some bids and asks to make the snapshot non‑trivial
        for (int i = 0; i < 10; ++i) {
            auto bid = create_order(i, OrderSide::BUY, 10000 + i * 10, 100);
            book->add_order(bid);
            auto ask = create_order(1000 + i, OrderSide::SELL, 10100 + i * 10, 100);
            book->add_order(ask);
        }
        manager = std::make_unique<BookSnapshotManager>(5);  // Capture top 5 levels
        manager->update(*book);
    }

    Order* create_order(uint64_t id, OrderSide side, int64_t price, uint32_t qty) {
        auto order = pool->acquire();
        order->order_id = id;
        order->side = side;
        order->price = price;
        order->quantity = qty;
        order->remaining_quantity = qty;
        order->filled_quantity = 0;
        std::strncpy(order->symbol, "AAPL", 7);
        Order* raw = order.get();
        owned_orders.push_back(std::move(order));
        return raw;
    }

    std::unique_ptr<lockfree::ObjectPool<Order, 100000>> pool;
    std::unique_ptr<OrderBook> book;
    std::unique_ptr<BookSnapshotManager> manager;
    std::vector<lockfree::PooledPtr<Order, 100000>> owned_orders;
};

static void BM_Snapshot_Update(benchmark::State& state) {
    BookSnapshotBench bench;
    for (auto _ : state) {
        bench.manager->update(*bench.book);
        benchmark::DoNotOptimize(bench.manager);
    }
}
BENCHMARK(BM_Snapshot_Update);

static void BM_Snapshot_Get(benchmark::State& state) {
    BookSnapshotBench bench;
    bench.manager->update(*bench.book);  // Initial snapshot
    for (auto _ : state) {
        const BookSnapshot* snap = bench.manager->get_snapshot();
        benchmark::DoNotOptimize(snap);
        bench.manager->release_snapshot(snap);
    }
}
BENCHMARK(BM_Snapshot_Get);

static void BM_Snapshot_GetAndTouch(benchmark::State& state) {
    BookSnapshotBench bench;
    bench.manager->update(*bench.book);
    for (auto _ : state) {
        const BookSnapshot* snap = bench.manager->get_snapshot();
        int64_t bid = snap->best_bid;
        int64_t ask = snap->best_ask;
        uint32_t depth = snap->bid_depth;
        // touch a few price levels
        if (!snap->bids.empty()) {
            benchmark::DoNotOptimize(snap->bids[0].price);
        }
        if (!snap->asks.empty()) {
            benchmark::DoNotOptimize(snap->asks[0].price);
        }
        benchmark::DoNotOptimize(bid);
        benchmark::DoNotOptimize(ask);
        benchmark::DoNotOptimize(depth);
        bench.manager->release_snapshot(snap);
    }
}
BENCHMARK(BM_Snapshot_GetAndTouch);

static void BM_Snapshot_Concurrent(benchmark::State& state) {
    const int num_readers = state.range(0);
    BookSnapshotBench bench;
    bench.manager->update(*bench.book);

    std::vector<std::thread> readers;
    std::atomic<bool> stop{false};
    std::atomic<uint64_t> total_ops{0};

    // Start reader threads
    for (int i = 0; i < num_readers; ++i) {
        readers.emplace_back([&]() {
            while (!stop.load(std::memory_order_relaxed)) {
                const BookSnapshot* snap = bench.manager->get_snapshot();
                if (snap) {
                    // Touch a field to simulate real work
                    benchmark::DoNotOptimize(snap->best_bid);
                    bench.manager->release_snapshot(snap);
                    total_ops.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }

    // Let the system stabilise
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // Run the benchmark: measure operations over a fixed time window
    for (auto _ : state) {
        uint64_t before = total_ops.load(std::memory_order_relaxed);
        // Run for a short, fixed duration
        auto start = std::chrono::high_resolution_clock::now();
        while (std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::high_resolution_clock::now() - start) < std::chrono::milliseconds(10)) {
            // Spin – wait for the time window to expire
        }
        uint64_t after = total_ops.load(std::memory_order_relaxed);
        state.SetIterationTime(0.01); // 10 ms
        state.SetItemsProcessed(after - before);
    }

    stop = true;
    for (auto& t : readers) t.join();
}
BENCHMARK(BM_Snapshot_Concurrent)->Arg(1)->Arg(2)->Arg(4)->Arg(8)->Arg(16);

static void BM_Snapshot_UpdateThroughput(benchmark::State& state) {
    BookSnapshotBench bench;
    for (auto _ : state) {
        bench.manager->update(*bench.book);
        benchmark::DoNotOptimize(bench.manager);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_Snapshot_UpdateThroughput);

static void BM_Snapshot_GetThroughput(benchmark::State& state) {
    BookSnapshotBench bench;
    bench.manager->update(*bench.book);
    for (auto _ : state) {
        const BookSnapshot* snap = bench.manager->get_snapshot();
        benchmark::DoNotOptimize(snap);
        bench.manager->release_snapshot(snap);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_Snapshot_GetThroughput);

static void BM_Snapshot_UpdateLatency(benchmark::State& state) {
    BookSnapshotBench bench;
    for (auto _ : state) {
        auto start = std::chrono::high_resolution_clock::now();
        bench.manager->update(*bench.book);
        auto end = std::chrono::high_resolution_clock::now();
        auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
        benchmark::DoNotOptimize(ns);
    }
}
BENCHMARK(BM_Snapshot_UpdateLatency);

BENCHMARK_MAIN();