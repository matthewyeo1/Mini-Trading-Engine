#include "velox/core/symbol_engine.hpp"
#include "velox/risk/risk_manager.hpp"
#include "velox/gateway/execution_gateway.hpp"
#include "velox/risk/position_manager.hpp"
#include "velox/feed/feed_handler.hpp"
#include <thread>
#include <vector>
#include <atomic>
#include <signal.h>
#include <iostream>

using namespace velox;

static std::atomic<bool> g_running{true};

void signal_handler(int) { g_running = false; }

int main() {
    // Signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // Market data ingestion (entry point)
    FeedHandler feed_handler;

    // Shared components
    RiskManager risk_manager;
    ExecutionGateway gateway;
    PositionManager pos_manager;

    // Add workers to Execution Gateway (1 per CPU core, tunable)
    const int num_workers = std::thread::hardware_concurrency();

    for (int i = 0; i < num_workers; ++i) {
        gateway.add_worker();
    }

    // Trading symbols
    std::vector<std::string> symbols = {"AAPL", "MSFT", "GOOGL", "AMZN", "META"};

    // One Symbol Engine per symbol
    std::vector<std::unique_ptr<SymbolEngine>> engines;
    std::vector<std::thread> worker_threads;

    for (const auto& s : symbols) {
        engines.push_back(std::make_unique<SymbolEngine>(
            s.c_str(), &risk_manager, &gateway, &pos_manager));
    }

    // Market data feed
    feed_handler.on_add_order([&](const Order& order) {
        
        // Linear search for matching symbol
        // TODO: use more efficient algorithm for larger symbol set
        for (auto& e : engines) {
            if (strcmp(e->order_book().symbol(), order.symbol) == 0) {

                // Each engine has its own pool (for now)
                static lockfree::ObjectPool<Order, 100000> global_pool;
                static std::vector<lockfree::PooledPtr<Order, 100000>> pending_orders;

                auto new_order = global_pool.acquire();
                *new_order = order;     // Copy
                std::cout << "[DEBUG] Routing to engine for symbol: " << e->order_book().symbol() << std::endl;
                e->on_market_order(new_order.get());
                pending_orders.push_back(std::move(new_order));
                break;
            }
        }
    });

    // Spawn a thread for each symbol's matching engine
    for (size_t i = 0; i < engines.size(); ++i) {
        worker_threads.emplace_back([&e = engines[i]]() {
            while (g_running) {
                e->run_match_cycle();

                // Small sleep cycles if no orders
                std::this_thread::sleep_for(std::chrono::microseconds(1));
            }
        });
    }

    // Process market data by simulating read from ITCH file
    // TODO: change to read from constant stream
    std::thread feed_thread([&]() {
        std::cout << "[DEBUG] Parsing mock ITCH file in test_data/NASDAQ_ITCH50_sample.bin..." << std::endl;
        feed_handler.process_file("test_data/NASDAQ_ITCH50_sample.bin");
        g_running = false;   // Stop workers when feed ends
    });

    // Spawn a thread to publish snapshots periodically
    // TODO: implement once current event loop works
    /*
    std::thread snapshot_thread([&]() {
        while (g_running) {
            for (auto& eng : engines) {

            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    });
    */
    
    // Wait for shutdown
    feed_thread.join();
    
    // Manually drain each engine's queue
    for (auto& e : engines) {
        e->run_match_cycle();   // run once more
    }
    // Give workers a chance (if still running)
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    g_running = false;
    
    // snapshot_thread.join();

    // Print final stats
    for (auto& e : engines) {
        auto stats = e->get_stats();

        
        std::cout << e->symbol() << ": matched=" << stats.orders_matched
                  << ", rejected=" << stats.orders_rejected
                  << ", partial=" << stats.orders_partially_filled
                  << ", avg_latency_ns=" << stats.avg_match_latency_ns
                  << std::endl;
        
    }

    return 0;

}