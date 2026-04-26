#include <benchmark/benchmark.h>
#include "velox/book/order_book.hpp"
#include "lockfree/pool.hpp"
#include <vector>

using namespace velox;
using Pool = lockfree::ObjectPool<Order, 200000>;
using Handle = lockfree::PooledPtr<Order, 200000>;

static Order* make_order(Pool& pool,
                         std::vector<Handle>& owned,
                         uint64_t id, OrderSide side,
                         int64_t price, uint32_t qty) {
    auto h = pool.acquire();
    h->order_id           = id;
    h->side               = side;
    h->price              = price;
    h->quantity           = qty;
    h->remaining_quantity = qty;
    h->filled_quantity    = 0;
    Order* raw = h.get();
    owned.push_back(std::move(h));
    return raw;
}

// Reset an order back to its original state for reuse across iterations
static void reset_order(Order* o, uint32_t qty) {
    o->remaining_quantity = qty;
    o->filled_quantity    = 0;
    o->status             = OrderStatus::NEW;
}

static void BM_OrderBook_AddBid(benchmark::State& state) {
    Pool pool;
    std::vector<Handle> owned;
    OrderBook book("AAPL");
    Order* order = make_order(pool, owned, 1, OrderSide::BUY, 10000, 100);

    for (auto _ : state) {
        book.add_order(order);
        benchmark::DoNotOptimize(book);
    }
}
BENCHMARK(BM_OrderBook_AddBid);

static void BM_OrderBook_AddAsk(benchmark::State& state) {
    Pool pool;
    std::vector<Handle> owned;
    OrderBook book("AAPL");
    Order* order = make_order(pool, owned, 1, OrderSide::SELL, 10100, 100);

    for (auto _ : state) {
        book.add_order(order);
        benchmark::DoNotOptimize(book);
    }
}
BENCHMARK(BM_OrderBook_AddAsk);

static void BM_OrderBook_AddMultipleLevels(benchmark::State& state) {
    const int N = state.range(0);
    Pool pool;

    for (auto _ : state) {
        state.PauseTiming();
        std::vector<Handle> owned;
        owned.reserve(N);
        OrderBook book("AAPL");
        std::vector<Order*> orders;
        orders.reserve(N);
        for (int i = 0; i < N; ++i)
            orders.push_back(make_order(pool, owned, i, OrderSide::BUY, 10000 + i, 100));
        state.ResumeTiming();

        for (int i = 0; i < N; ++i)
            book.add_order(orders[i]);

        benchmark::DoNotOptimize(book);
        // owned destructs at end of iteration → slots returned to pool
    }
}
BENCHMARK(BM_OrderBook_AddMultipleLevels)->Arg(10)->Arg(100)->Arg(1000);

static void BM_OrderBook_MatchBuy(benchmark::State& state) {
    Pool pool;
    std::vector<Handle> owned;

    // Resting sell orders — created once, reset each iteration
    std::vector<Order*> sells;
    sells.reserve(100);

    std::vector<Fill> fills;
    fills.reserve(100);

    for (int i = 0; i < 100; ++i)
        sells.push_back(make_order(pool, owned, i, OrderSide::SELL, 10000 - i, 100));

    // Incoming buy — created once, reset each iteration
    Order* buy = make_order(pool, owned, 10000, OrderSide::BUY, 10100, 10000);

    for (auto _ : state) {
        state.PauseTiming();
        std::vector<Handle> owned;
        OrderBook book("AAPL");
        for (auto* s : sells) {
            reset_order(s, 100);
            book.add_order(s);
        }
        reset_order(buy, 10000);
        state.ResumeTiming();  

        fills.clear();
        auto remaining = book.match(buy, fills);
        benchmark::DoNotOptimize(remaining);
    }
}
BENCHMARK(BM_OrderBook_MatchBuy);

static void BM_OrderBook_MatchSell(benchmark::State& state) {
    Pool pool;
    std::vector<Handle> owned;

    std::vector<Order*> buys;
    buys.reserve(100);

    std::vector<Fill> fills;
    fills.reserve(100);

    for (int i = 0; i < 100; ++i)
        buys.push_back(make_order(pool, owned, i, OrderSide::BUY, 10000 + i, 100));

    Order* sell = make_order(pool, owned, 10000, OrderSide::SELL, 9900, 10000);

    for (auto _ : state) {
        state.PauseTiming();
        OrderBook book("AAPL");
        for (auto* b : buys) {
            reset_order(b, 100);
            book.add_order(b);
        }

        fills.clear();
        reset_order(sell, 10000);
        state.ResumeTiming();

        auto remaining = book.match(sell, fills);
        benchmark::DoNotOptimize(remaining);
    }
}
BENCHMARK(BM_OrderBook_MatchSell);

static void BM_OrderBook_MarketOrder(benchmark::State& state) {
    Pool pool;
    std::vector<Handle> owned;

    std::vector<Order*> sells;
    sells.reserve(100);

    std::vector<Fill> fills;
    fills.reserve(100);
    
    for (int i = 0; i < 100; ++i)
        sells.push_back(make_order(pool, owned, i, OrderSide::SELL, 10000 + i * 10, 100));

    Order* buy = make_order(pool, owned, 10000, OrderSide::BUY, 11000, 10000);

    for (auto _ : state) {
        state.PauseTiming();
        OrderBook book("AAPL");
        for (auto* s : sells) {
            reset_order(s, 100);
            book.add_order(s);
        }
        reset_order(buy, 10000);
        state.ResumeTiming();

        fills.clear();
        auto remaining = book.match(buy, fills);
        benchmark::DoNotOptimize(remaining);
    }
}
BENCHMARK(BM_OrderBook_MarketOrder);

static void BM_OrderBook_Cancel(benchmark::State& state) {
    Pool pool;
    std::vector<Handle> owned;

    for (auto _ : state) {
        state.PauseTiming();
        std::vector<Handle> owned;  // Destruct each iteration
        OrderBook book("AAPL");
        Order* order = make_order(pool, owned, 1, OrderSide::BUY, 10000, 100);
        book.add_order(order);
        state.ResumeTiming();

        book.cancel_order(1);
        benchmark::DoNotOptimize(book);
    }
}
BENCHMARK(BM_OrderBook_Cancel);

static void BM_OrderBook_BestPrices(benchmark::State& state) {
    Pool pool;
    std::vector<Handle> owned;
    OrderBook book("AAPL");

    book.add_order(make_order(pool, owned, 1, OrderSide::BUY,  10000, 100));
    book.add_order(make_order(pool, owned, 2, OrderSide::SELL, 10100, 100));

    for (auto _ : state) {
        int64_t bid = book.best_bid();
        int64_t ask = book.best_ask();
        benchmark::DoNotOptimize(bid);
        benchmark::DoNotOptimize(ask);
    }
}
BENCHMARK(BM_OrderBook_BestPrices);

BENCHMARK_MAIN();