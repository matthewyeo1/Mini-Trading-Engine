#include "velox/core/symbol_engine.hpp"
#include "velox/risk/risk_manager.hpp"
#include "velox/gateway/execution_gateway.hpp"
#include "velox/risk/position_manager.hpp"
#include "velox/feed/feed_handler.hpp"
#include "velox/matching/matching_engine.hpp"
#include "velox/sim/strategy/bot_manager.hpp"
#include "velox/sim/strategy/bots.hpp"
#include "velox/sim/env/market_sim.hpp"

#include <thread>
#include <vector>
#include <atomic>
#include <signal.h>
#include <iostream>
#include <chrono>
#include <cstring>

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

    // Initialize simulated market environment
    env::MarketSimulator simulator(100.0, 10000, 50, 5, 50, 500);

    // Gateway workers
    const int num_workers = std::thread::hardware_concurrency();
    for (int i = 0; i < num_workers; ++i) {
        gateway.add_worker();
    }

    // Symbols
    std::vector<std::string> symbols = {"AAPL", "MSFT", "GOOGL", "AMZN", "META"};

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

    // Snapshot + bot loop
    std::thread snapshot_thread([&]() {
        std::unordered_map<std::string, std::unique_ptr<BookSnapshotManager>> snaps;
        for (auto& e : engines) {
            snaps[e->symbol()] = std::make_unique<BookSnapshotManager>(5);
        }

        static lockfree::ObjectPool<Order, 100000> bot_pool;
        static std::vector<lockfree::PooledPtr<Order, 100000>> bot_storage;

        while (g_running) {
            for (auto& e : engines) {
                auto& sm = *snaps[e->symbol()];
                sm.update(e->order_book());

                const auto* snap = sm.get_snapshot();
                if (snap && snap->valid()) {
                    bot_manager->on_snapshot(*snap);
                    sm.release_snapshot(snap);
                }

                // Process bot orders
                Order tmp;
                while (bot_manager->pop_order(tmp)) {
                    auto ptr = bot_pool.acquire();
                    *ptr = tmp;
                    ptr->remaining_quantity = ptr->quantity;
                    ptr->filled_quantity = 0;
                    ptr->status = OrderStatus::NEW;
                    for (auto& eng : engines) {
                        if (strcmp(eng->symbol(), ptr->symbol) == 0) {
                            eng->on_market_order(ptr.get());
                            bot_storage.push_back(std::move(ptr));
                            break;
                        }
                    }
                }

                /* Periodically drain bot_storage of orders already processed (O(n))
                // Sweep bot storage array
                auto it = bot_storage.begin();

                while (it != bot_storage.end()) {
                    if ((*it)->status != OrderStatus::NEW) {
                        it = bot_storage.erase(it);
                        // std::cout << "[CLEAN] bot_storage slot returned to pool" << std::endl;
                    } else {
                        ++it;
                    }
                }
                */
            }

            // Swap processed orders to separate list & clear (faster than O(n))
            std::vector<lockfree::PooledPtr<Order, 100000>> next_storage;
            next_storage.reserve(bot_storage.size());

            for (auto& ptr : bot_storage) {
                if (ptr->status == OrderStatus::NEW || ptr->status == OrderStatus::PARTIAL) {
                    next_storage.push_back(std::move(ptr));
                }
                // Finished orders: ptr destructs, slot returned to pool
            }

            // Swap back
            bot_storage = std::move(next_storage);

            if (!g_running) break;

            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        std::cout << "Exiting snapshot thread" << std::endl;
    });

    // Start simulator
    simulator.start(engines, [](SymbolEngine& engine, int64_t bid, int64_t ask) {
        static std::unordered_map<std::string, int> counters;
        int& count = counters[engine.symbol()];

        // Log every 100th tick
        if (++count % 100 == 0) {
            std::cout << "============= COUNT " << count << " ==============" << std::endl;
            std::cout << engine.symbol() << " bid=" << bid << " ask=" << ask << std::endl;
        }
    });

    std::cout << "=== MARKET OPEN ===" << std::endl;
    std::cout << "Trading for " << duration_minutes << " minutes..." << std::endl;
    std::this_thread::sleep_for(std::chrono::minutes(duration_minutes));
    std::cout << "=== MARKET CLOSE ===" << std::endl;

    g_running = false;

    // Final drain
    for (auto& e : engines) {
        for (int i = 0; i < 10; ++i) {
            e->run_match_cycle();
        }
    }

    simulator.stop();

    if (snapshot_thread.joinable()) {
        snapshot_thread.join();
    }

    for (auto& t : workers) t.join();

    std::cout << "\n=== FINAL STATS ===\n";
    for (auto& e : engines) {
        auto s = e->get_stats();
        std::cout << e->symbol()
                  << " matched=" << s.orders_matched
                  << " rejected=" << s.orders_rejected
                  << " partial=" << s.orders_partially_filled
                  << " bid="      << e->order_book().best_bid()
                  << " ask="      << e->order_book().best_ask()
                  << " bid_depth="<< e->order_book().bid_depth()
                  << " ask_depth="<< e->order_book().ask_depth()
                  << "\n";
    }

    return 0;
}
