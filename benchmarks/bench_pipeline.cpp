#include <benchmark/benchmark.h>
#include "velox/core/symbol_engine.hpp"
#include "velox/risk/risk_manager.hpp"
#include "velox/gateway/execution_gateway.hpp"
#include "velox/risk/position_manager.hpp"
#include "velox/feed/feed_handler.hpp"
#include "lockfree/pool.hpp"

using namespace velox;

static void BM_Pipeline_ProcessFile(benchmark::State& state) {
    for (auto _ : state) {
        // Fresh components for each iteration
        RiskManager risk;
        ExecutionGateway gateway;
        PositionManager pos_mgr;
        FeedHandler feed;
        
        // Add workers
        for (int i = 0; i < 4; ++i) gateway.add_worker();
        
        // Create engines for symbols that appear in the file
        std::vector<std::unique_ptr<SymbolEngine>> engines;
        engines.push_back(std::make_unique<SymbolEngine>("AAPL", &risk, &gateway, &pos_mgr));
        engines.push_back(std::make_unique<SymbolEngine>("MSFT", &risk, &gateway, &pos_mgr));
        engines.push_back(std::make_unique<SymbolEngine>("GOOGL", &risk, &gateway, &pos_mgr));
        
        // Route orders from feed to correct engine
        feed.on_add_order([&](const Order& order) {
            for (auto& e : engines) {
                if (strcmp(e->order_book().symbol(), order.symbol) == 0) {
                    static lockfree::ObjectPool<Order, 100000> pool;
                    static std::vector<lockfree::PooledPtr<Order, 100000>> pending;
                    auto new_order = pool.acquire();
                    *new_order = order;
                    e->on_market_order(new_order.get());
                    pending.push_back(std::move(new_order));
                    break;
                }
            }
        });
        
        // Process the mock file (or a larger test file)
        feed.process_file("test_data/NASDAQ_ITCH50_sample.bin");
        
        // Drain engines
        for (auto& e : engines) e->run_match_cycle();
        
        benchmark::DoNotOptimize(&gateway);
    }
}
BENCHMARK(BM_Pipeline_ProcessFile);

BENCHMARK_MAIN();