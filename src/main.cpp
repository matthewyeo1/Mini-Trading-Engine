#include "velox/core/symbol_engine.hpp"
#include "velox/risk/risk_manager.hpp"
#include "velox/gateway/execution_gateway.hpp"
#include "velox/risk/position_manager.hpp"
#include "velox/feed/feed_handler.hpp"
#include "velox/matching/matching_engine.hpp"
#include "velox/sim/strategy/bot_manager.hpp"
#include "velox/sim/strategy/bots.hpp"

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

int main() {
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

    std::vector<std::unique_ptr<SymbolEngine>> engines;
    for (const auto& s : symbols) {
        engines.push_back(std::make_unique<SymbolEngine>(
            s.c_str(), &risk_manager, &gateway, &pos_manager));
    }

    // Bots
    bot_manager->register_bot(std::make_unique<bot::MarketMakerBot>("MM_AAPL", "AAPL", 10000, 100, 500));
    bot_manager->register_bot(std::make_unique<bot::SpreadBot>("Spread_AAPL", "AAPL"));
    bot_manager->register_bot(std::make_unique<bot::RandomWalkBot>("RW_AAPL", "AAPL"));
    bot_manager->register_bot(std::make_unique<bot::MeanReversionBot>("MR_AAPL", "AAPL"));
    bot_manager->register_bot(std::make_unique<bot::MomentumBot>("Mom_AAPL", "AAPL"));

    // Shared pools
    static lockfree::ObjectPool<Order, 100000> global_pool;
    static std::vector<lockfree::PooledPtr<Order, 100000>> order_storage;

    // Feed handler routing
    feed_handler.on_add_order([&](const Order& order) {
        for (auto& e : engines) {
            if (strcmp(e->order_book().symbol(), order.symbol) == 0) {
                auto ptr = global_pool.acquire();
                *ptr = order;

                // CRITICAL: normalize state
                ptr->remaining_quantity = ptr->quantity;
                ptr->filled_quantity = 0;
                ptr->status = OrderStatus::NEW;

                e->on_market_order(ptr.get());
                order_storage.push_back(std::move(ptr));
                break;
            }
        }
    });

    std::cout << "[MAIN] Seeding market with extreme prices (bid=1, ask=999999)...\n";

    static lockfree::ObjectPool<Order, 100000> seed_pool;
    static std::vector<lockfree::PooledPtr<Order, 100000>> seed_storage;

    for (auto& e : engines) {
        // BID at $0.01 (price=1) – extremely low, never matched by any sell order
        auto bid = seed_pool.acquire();
        bid->order_id = 1000;
        bid->side = OrderSide::BUY;
        bid->price = 1;
        bid->quantity = 1000;
        bid->remaining_quantity = 1000;
        bid->filled_quantity = 0;
        bid->status = OrderStatus::NEW;
        std::strncpy(bid->symbol, e->symbol(), 7);
        e->get_order_book().add_order(bid.get());      // NOTE: populate order book WITHOUT going through matching engine (otherwise rejection)
        seed_storage.push_back(std::move(bid));

        // ASK at $9999.99 (price=999999) – extremely high, never matched by any buy order
        auto ask = seed_pool.acquire();
        ask->order_id = 1001;
        ask->side = OrderSide::SELL;
        ask->price = 999999;
        ask->quantity = 1000;
        ask->remaining_quantity = 1000;
        ask->filled_quantity = 0;
        ask->status = OrderStatus::NEW;
        std::strncpy(ask->symbol, e->symbol(), 7);
        e->get_order_book().add_order(ask.get());
        seed_storage.push_back(std::move(ask));
    }

    // Verify seed orders are in the book
    for (auto& e : engines) {
        std::cout << "[POST-SEED] " << e->symbol()
                << " bid=" << e->order_book().best_bid()
                << " ask=" << e->order_book().best_ask() << "\n";
    }

    // Process seed orders
    for (auto& e : engines) {
        for (int i = 0; i < 10; ++i) {
            e->run_match_cycle();
        }
    }

    // Verification
    for (auto& e : engines) {
        std::cout << "[POST-SEED] " << e->symbol()
                  << " seq=" << e->order_book().sequence()
                  << " bid=" << e->order_book().best_bid()
                  << " ask=" << e->order_book().best_ask() << "\n";
    }

    // Worker threads
    std::vector<std::thread> workers;
    for (auto& e : engines) {
        workers.emplace_back([&e]() {
            while (g_running) {
                e->run_match_cycle();
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
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    });

    // Feed thread (process ITCH file)
    std::thread feed_thread([&]() {
        feed_handler.process_file("test_data/NASDAQ_ITCH50_sample.bin");
    });

    feed_thread.join();

    std::this_thread::sleep_for(std::chrono::seconds(2));
    g_running = false;

    snapshot_thread.join();

    for (auto& t : workers) t.join();

    // Final drain
    for (auto& e : engines) {
        for (int i = 0; i < 10; ++i) {
            e->run_match_cycle();
        }
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
