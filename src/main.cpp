#include "velox/core/symbol_engine.hpp"
#include "velox/risk/risk_manager.hpp"
#include "velox/gateway/execution_gateway.hpp"
#include "velox/risk/position_manager.hpp"
#include "velox/feed/feed_handler.hpp"
#include "velox/matching/matching_engine.hpp"
#include "velox/sim/strategy/bot_manager.hpp"
#include "velox/sim/strategy/bots.hpp"
#include "velox/sim/env/market_sim.hpp"
#include "velox/sim/env/market_state_store.hpp"

#include <thread>
#include <vector>
#include <atomic>
#include <signal.h>
#include <iostream>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <mutex>

using namespace velox;

static std::atomic<bool> g_running{true};
void signal_handler(int) { g_running = false; }

int main(int argc, char* argv[]) {

    // Parse second argument for desired trading window in minutes (default is 10 minutes)
    int duration_minutes = (argc > 1) ? std::stoi(argv[1]) : 10;

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    FeedHandler feed_handler;
    RiskManager risk_manager;
    ExecutionGateway gateway;
    PositionManager pos_manager;

    auto bot_manager = std::make_unique<bot::BotManager>();

    // Gateway workers
    const int num_workers = std::thread::hardware_concurrency();
    for (int i = 0; i < num_workers; ++i) {
        gateway.add_worker();
    }

    // Symbols
    std::vector<std::string> symbols = {"AAPL", "MSFT", "GOOGL", "AMZN", "META"};

    // Price per share for each symbol
    const std::unordered_map<std::string, int64_t> default_initial_prices = {
        {"AAPL", 17500},   // $175.00
        {"MSFT", 33000},   // $330.00
        {"GOOGL", 12500},  // $125.00
        {"AMZN", 13500},   // $135.00
        {"META", 30000}    // $300.00
    };

    const std::filesystem::path state_file_path = std::filesystem::current_path() / "market_state.txt";
    std::unordered_map<std::string, int64_t> initial_prices =
        velox::env::read_market_state(state_file_path.string(), symbols, default_initial_prices);

    std::unordered_map<std::string, int64_t> last_updated_prices = initial_prices;
    std::mutex last_updated_prices_mutex;

    // Initialize simulated market environment
    env::MarketSimulator simulator(100.0, initial_prices, 50, 5, 50, 500);

    std::vector<std::unique_ptr<SymbolEngine>> engines;
    for (const auto& s : symbols) {
        engines.push_back(std::make_unique<SymbolEngine>(
            s.c_str(), &risk_manager, &gateway, &pos_manager));
    }

    // Bots for each symbol

    // AAPL
    bot_manager->register_bot(std::make_unique<bot::MarketMakerBot>("MM_AAPL", "AAPL", 10000, 100, 500));
    bot_manager->register_bot(std::make_unique<bot::SpreadBot>("Spread_AAPL", "AAPL"));
    bot_manager->register_bot(std::make_unique<bot::RandomWalkBot>("RW_AAPL", "AAPL"));
    bot_manager->register_bot(std::make_unique<bot::MeanReversionBot>("MR_AAPL", "AAPL"));
    bot_manager->register_bot(std::make_unique<bot::MomentumBot>("Mom_AAPL", "AAPL"));

    // MSFT
    bot_manager->register_bot(std::make_unique<bot::MarketMakerBot>("MM_MSFT", "MSFT", 10000, 100, 500));
    bot_manager->register_bot(std::make_unique<bot::SpreadBot>("Spread_MSFT", "MSFT"));
    bot_manager->register_bot(std::make_unique<bot::RandomWalkBot>("RW_MSFT", "MSFT"));
    bot_manager->register_bot(std::make_unique<bot::MeanReversionBot>("MR_MSFT", "MSFT"));
    bot_manager->register_bot(std::make_unique<bot::MomentumBot>("Mom_MSFT", "MSFT"));

    // GOOGL
    bot_manager->register_bot(std::make_unique<bot::MarketMakerBot>("MM_GOOGL", "GOOGL", 10000, 100, 500));
    bot_manager->register_bot(std::make_unique<bot::SpreadBot>("Spread_GOOGL", "GOOGL"));
    bot_manager->register_bot(std::make_unique<bot::RandomWalkBot>("RW_GOOGL", "GOOGL"));
    bot_manager->register_bot(std::make_unique<bot::MeanReversionBot>("MR_GOOGL", "GOOGL"));
    bot_manager->register_bot(std::make_unique<bot::MomentumBot>("Mom_GOOGL", "GOOGL"));

    // AMZN
    bot_manager->register_bot(std::make_unique<bot::MarketMakerBot>("MM_AMZN", "AMZN", 10000, 100, 500));
    bot_manager->register_bot(std::make_unique<bot::SpreadBot>("Spread_AMZN", "AMZN"));
    bot_manager->register_bot(std::make_unique<bot::RandomWalkBot>("RW_AMZN", "AMZN"));
    bot_manager->register_bot(std::make_unique<bot::MeanReversionBot>("MR_AMZN", "AMZN"));
    bot_manager->register_bot(std::make_unique<bot::MomentumBot>("Mom_AMZN", "AMZN"));

    // META
    bot_manager->register_bot(std::make_unique<bot::MarketMakerBot>("MM_META", "META", 10000, 100, 500));
    bot_manager->register_bot(std::make_unique<bot::SpreadBot>("Spread_META", "META"));
    bot_manager->register_bot(std::make_unique<bot::RandomWalkBot>("RW_META", "META"));
    bot_manager->register_bot(std::make_unique<bot::MeanReversionBot>("MR_META", "META"));
    bot_manager->register_bot(std::make_unique<bot::MomentumBot>("Mom_META", "META"));

    // Worker threads
    std::vector<std::thread> workers;
    for (auto& e : engines) {
        workers.emplace_back([&e]() {
            while (g_running) {
                e->run_match_cycle();
                std::this_thread::yield();
            }
        });
    }

    // Snapshot threads: one per symbol, each with its own pool and storage
    std::vector<std::thread> snapshot_threads;
    for (auto& e : engines) {
        snapshot_threads.emplace_back([&, symbol = std::string(e->symbol())]() {
            BookSnapshotManager sm(5);
            lockfree::ObjectPool<Order, 100000> bot_pool;
            std::vector<lockfree::PooledPtr<Order, 100000>> bot_storage;

            while (g_running) {
                // Update snapshot for this engine only
                auto eng_ptr = std::find_if(engines.begin(), engines.end(), [&](const std::unique_ptr<SymbolEngine>& pe){ return symbol == pe->symbol(); });
                if (eng_ptr == engines.end()) break;
                auto& eng = *eng_ptr->get();

                sm.update(eng.order_book());
                const auto* snap = sm.get_snapshot();
                if (snap && snap->valid()) {
                    bot_manager->on_snapshot(*snap);
                    sm.release_snapshot(snap);
                }

                // Process bot orders for this symbol only
                Order tmp;
                while (bot_manager->pop_order_for_symbol(symbol, tmp)) {
                    auto ptr = bot_pool.acquire();
                    if (!ptr) {
                        // Pool exhausted: drop order and continue
                        continue;
                    }
                    *ptr = tmp;
                    ptr->remaining_quantity = ptr->quantity;
                    ptr->filled_quantity = 0;
                    ptr->status = OrderStatus::NEW;
                    eng.on_market_order(ptr.get());
                    bot_storage.push_back(std::move(ptr));
                }

                // Sweep finished orders
                std::vector<lockfree::PooledPtr<Order, 100000>> next_storage;
                next_storage.reserve(bot_storage.size());
                for (auto& ptr : bot_storage) {
                    if (ptr->status == OrderStatus::NEW || ptr->status == OrderStatus::PARTIAL) {
                        next_storage.push_back(std::move(ptr));
                    }
                }
                bot_storage = std::move(next_storage);

                if (!g_running) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }

            std::cout << "Exiting snapshot thread for " << symbol << std::endl;
        });
    }

    // Cancel processing thread: centralizes cancels and routes to engines
    std::thread cancel_thread([&]() {
        while (g_running) {
            uint64_t cancel_id;
            while (bot_manager->pop_cancel(cancel_id)) {
                for (auto& eng : engines) {
                    if (eng->cancel_order(cancel_id)) break;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        // Drain remaining cancels at shutdown
        uint64_t cancel_id;
        while (bot_manager->pop_cancel(cancel_id)) {
            for (auto& eng : engines) {
                if (eng->cancel_order(cancel_id)) break;
            }
        }
        std::cout << "Exiting cancel thread" << std::endl;
    });

    // Start simulator
    simulator.start(engines, [&](SymbolEngine& engine, int64_t bid, int64_t ask) {
        static std::unordered_map<std::string, int> counters;
        int& count = counters[engine.symbol()];

        // Log every 100th tick
        if (++count % 100 == 0) {
            std::cout << "============= COUNT " << count << " ==============" << std::endl;
            std::cout << engine.symbol() << " bid=" << bid << " ask=" << ask << std::endl;
        }

        const int64_t mid_price = (bid + ask) / 2;
        {
            std::lock_guard<std::mutex> lock(last_updated_prices_mutex);
            last_updated_prices[engine.symbol()] = mid_price;
            velox::env::write_market_state(state_file_path.string(), last_updated_prices);
        }
    });

    std::cout << "=== MARKET OPEN ===" << std::endl;
    std::cout << "Trading for " << duration_minutes << " minutes..." << std::endl;
    const auto market_close_time = std::chrono::steady_clock::now() + std::chrono::minutes(duration_minutes);
    while (g_running && std::chrono::steady_clock::now() < market_close_time) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    std::cout << "=== MARKET CLOSE ===" << std::endl;

    g_running = false;

    {
        std::lock_guard<std::mutex> lock(last_updated_prices_mutex);
        for (const auto& symbol : symbols) {
            if (last_updated_prices.find(symbol) == last_updated_prices.end()) {
                last_updated_prices[symbol] = initial_prices[symbol];
            }
        }
        velox::env::write_market_state(state_file_path.string(), last_updated_prices);
    }
    std::cout << "[SHUTDOWN] persisted symbol values to " << state_file_path << std::endl;

    std::cout << "[SHUTDOWN] calling simulator.stop()" << std::endl;
    simulator.stop();
    std::cout << "[SHUTDOWN] simulator.stop() returned" << std::endl;

    // Join snapshot threads
    std::cout << "[SHUTDOWN] joining snapshot threads" << std::endl;
    for (auto& t : snapshot_threads) {
        if (t.joinable()) t.join();
    }
    std::cout << "[SHUTDOWN] snapshot threads joined" << std::endl;

    // Join cancel thread
    if (cancel_thread.joinable()) {
        std::cout << "[SHUTDOWN] joining cancel_thread" << std::endl;
        cancel_thread.join();
        std::cout << "[SHUTDOWN] cancel_thread joined" << std::endl;
    }

    std::cout << "[SHUTDOWN] joining " << workers.size() << " worker threads" << std::endl;
    for (auto& t : workers) {
        t.join();
    }
    std::cout << "[SHUTDOWN] worker threads joined" << std::endl;
    
    // Final drain
    for (auto& e : engines) {
        e->drain();
    }

    std::cout << "\n=== FINAL STATS ===\n";
    for (auto& e : engines) {
        auto s = e->get_stats();
        std::cout << e->symbol()
                  << " matched=" << s.orders_matched
                  << " rejected=" << s.orders_rejected
                  << " partial=" << s.orders_partially_filled
                  << "\n";
    }

    return 0;
}
