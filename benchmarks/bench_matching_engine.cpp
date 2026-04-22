#include <benchmark/benchmark.h>
#include "velox/book/order_book.hpp"
#include "velox/matching/matching_engine.hpp"
#include "velox/risk/risk_manager.hpp"
#include "velox/gateway/execution_gateway.hpp"
#include "lockfree/pool.hpp"
#include <vector>

using namespace velox;

using Pool = lockfree::ObjectPool<Order, 200000>;
using Handle = lockfree::PooledPtr<Order, 200000>;

static Order* make_order(Pool& pool,
                        std::vector<Handle>& owned,
                        uint64_t id,
                        OrderSide side,
                        int64_t price,
                        uint32_t qty) {
    auto h = pool.acquire();
    h->order_id = id;
    h->side = side;
    h->price = price;
    h->quantity = qty;
    h->remaining_quantity = qty;
    h->filled_quantity = 0;
    std::strncpy(h->symbol, "AAPL", 7);
    Order* raw = h.get();
    owned.push_back(std::move(h));
    return raw;
}

static void reset_order(Order* o, uint32_t qty) {
    o->remaining_quantity = qty;
    o->filled_quantity = 0;
    o->status = OrderStatus::NEW;
}

// Case 1: No I/O cost gateway; pure matching latency
class NullGateway : public ExecutionGateway {
public:
    bool send_report(const ExecutionReport& report, int worker_id = -1) {
        (void)report;
        (void)worker_id;
        return true;
    }
    bool send_order(const Order* order, int worker_id = -1) {
        (void)order;
        (void)worker_id;
        return true;
    }
    void add_worker() {}
};

// Case 2: Actual gateway with queues
class RealGateway : public ExecutionGateway {
public:
    RealGateway() {
        add_worker();
        add_worker();
    }
};

static void BM_OrderBook_MatchOnly(benchmark::State& state) {
    Pool pool;
    std::vector<Handle> owned;

    std::vector<Order*> sells;
    sells.reserve(100);

    for (int i = 0; i < 100; ++i) {
        sells.push_back(make_order(pool, owned, i, OrderSide::SELL, 10000 + i, 100));
    }

    Order* buy = make_order(pool, owned, 99999, OrderSide::BUY, 20000, 10000);

    for (auto _ : state) {
        state.PauseTiming();
        OrderBook book("AAPL");
        for (auto* s : sells) {
            reset_order(s, 100);
            book.add_order(s);
        }
        reset_order(buy, 10000);
        state.ResumeTiming();

        book.match(buy);
        benchmark::DoNotOptimize(book);
    }
}
BENCHMARK(BM_OrderBook_MatchOnly);

static void BM_MatchingEngine_NoRisk_NoGateway(benchmark::State& state) {
    Pool pool;
    std::vector<Handle> owned;
    NullGateway gateway;
    MatchingEngine engine("AAPL", nullptr, &gateway);

    const int N = state.range(0);
    std::vector<Order*> orders;
    orders.reserve(N);

    for (int i = 0; i < N; ++i) {
        orders.push_back(make_order(pool, owned, i, OrderSide::BUY, 10000 + i, 100));
    }

    for (auto _ : state) {
        state.PauseTiming();
        for (auto* o : orders) reset_order(o, 100);
        state.ResumeTiming();

        for (auto* o : orders) engine.submit_order(o);
        engine.run_match_cycle();
        benchmark::DoNotOptimize(engine);
    }
}
BENCHMARK(BM_MatchingEngine_NoRisk_NoGateway)->Arg(10)->Arg(100)->Arg(500)->Arg(1000);

static void BM_MatchingEngine_WithRisk_NoGateway(benchmark::State& state) {
    Pool pool;
    std::vector<Handle> owned;
    RiskManager risk;
    NullGateway gateway;
    MatchingEngine engine("AAPL", &risk, &gateway);

    const int N = state.range(0);
    std::vector<Order*> orders;
    orders.reserve(N);

    for (int i = 0; i < N; ++i) {
        orders.push_back(make_order(pool, owned, i, OrderSide::BUY, 10000 + i, 100));
    }

    for (auto _ : state) {
        state.PauseTiming();
        for (auto* o : orders) reset_order(o, 100);
        state.ResumeTiming();

        for (auto* o : orders) engine.submit_order(o);
        engine.run_match_cycle();
        benchmark::DoNotOptimize(engine);
    }
}
BENCHMARK(BM_MatchingEngine_WithRisk_NoGateway)->Arg(10)->Arg(100)->Arg(500)->Arg(1000);

static void BM_MatchingEngine_NoRisk_RealGateway(benchmark::State& state) {
    Pool pool;
    std::vector<Handle> owned;
    RealGateway gateway;
    MatchingEngine engine("AAPL", nullptr, &gateway);

    const int N = state.range(0);
    std::vector<Order*> orders;
    orders.reserve(N);

    for (int i = 0; i < N; ++i) {
        orders.push_back(make_order(pool, owned, i, OrderSide::BUY, 10000 + i, 100));
    }

    for (auto _ : state) {
        state.PauseTiming();
        for (auto* o : orders) reset_order(o, 100);
        state.ResumeTiming();

        for (auto* o : orders) engine.submit_order(o);
        engine.run_match_cycle();
        benchmark::DoNotOptimize(engine);
    }
}
BENCHMARK(BM_MatchingEngine_NoRisk_RealGateway)->Arg(10)->Arg(100)->Arg(500)->Arg(1000);

static void BM_MatchingEngine_FullPipeline(benchmark::State& state) {
    Pool pool;
    std::vector<Handle> owned;
    RiskManager risk;
    RealGateway gateway;
    MatchingEngine engine("AAPL", &risk, &gateway);

    const int N = state.range(0);
    std::vector<Order*> orders;
    orders.reserve(N);

    for (int i = 0; i < N; ++i) {
        orders.push_back(make_order(pool, owned, i, OrderSide::BUY, 10000 + i, 100));
    }

    for (auto _ : state) {
        state.PauseTiming();
        for (auto* o : orders) reset_order(o, 100);
        state.ResumeTiming();

        for (auto* o : orders) engine.submit_order(o);
        engine.run_match_cycle();
        benchmark::DoNotOptimize(engine);
    }
}
BENCHMARK(BM_MatchingEngine_FullPipeline)->Arg(10)->Arg(100)->Arg(500)->Arg(1000);

static void BM_MatchingEngine_Throughput(benchmark::State& state) {
    Pool pool;
    std::vector<Handle> owned;
    RiskManager risk;
    RealGateway gateway;
    MatchingEngine engine("AAPL", &risk, &gateway);

    const int N = 1000;
    std::vector<Order*> orders;
    orders.reserve(N);

    for (int i = 0; i < N; ++i) {
        orders.push_back(make_order(pool, owned, i, OrderSide::BUY, 10000 + (i % 50), 100));
    }

    for (auto _ : state) {
        state.PauseTiming();
        for (auto* o : orders) reset_order(o, 100);
        state.ResumeTiming();

        for (auto* o : orders) engine.submit_order(o);
        engine.run_match_cycle();
    }

    state.SetItemsProcessed(state.iterations() * N);
}
BENCHMARK(BM_MatchingEngine_Throughput);

BENCHMARK_MAIN();