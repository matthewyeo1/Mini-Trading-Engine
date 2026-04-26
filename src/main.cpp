#include "velox/core/symbol_engine.hpp"
#include "velox/risk/risk_manager.hpp"
#include "velox/gateway/execution_gateway.hpp"
#include "velox/risk/position_manager.hpp"
#include "velox/feed/feed_handler.hpp"
#include "velox/matching/matching_engine.hpp"
#include <thread>
#include <vector>
#include <atomic>
#include <signal.h>
#include <iostream>
#include <chrono>
#include <ctime>

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
                // std::cout << "[DEBUG] Routing to engine for symbol: " << e->order_book().symbol() << std::endl;
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

    // Spawn a thread for stats reporting
    std::thread stats_thread([&]() {
        while (g_running) {
            std::this_thread::sleep_for(std::chrono::seconds(5));  // N = 5 seconds (arbitrary)
            
            std::cout << "\n=== Trading Engine Stats [" 
                      << std::chrono::system_clock::to_time_t(std::chrono::system_clock::now())
                      << "] ===" << std::endl;
            
            for (auto& e : engines) {
                auto stats = e->get_stats();
                int64_t position = pos_manager.get_position(e->symbol());
                int64_t realized_pnl = pos_manager.get_realized_pnl(e->symbol());
                
                std::cout << "  " << e->symbol() 
                          << ": matched=" << stats.orders_matched
                          << ", position=" << position
                          << ", realized_PnL=$" << (realized_pnl / 100.0)
                          << ", rejected=" << stats.orders_rejected
                          << ", partial=" << stats.orders_partially_filled
                          << std::endl;
            }
            
            std::cout << "  Gateway: total_reports=" << gateway.total_reports_sent() 
                      << ", total_orders=" << gateway.total_orders_sent() << std::endl;
        }
    });

    // Process market data by simulating read from ITCH file
    // TODO: change to read from constant stream
    std::thread feed_thread([&]() {
        // std::cout << "[DEBUG] Parsing mock ITCH file in test_data/NASDAQ_ITCH50_sample.bin..." << std::endl;
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
    g_running = false;

    // Fully drain each engine's queue before shutting down workers
    for (auto& e : engines) {
        e->run_match_cycle();
    }

    // Let stats reporting thread print before yielding
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Join stats thread
    if (stats_thread.joinable()) {
        stats_thread.join();
    }
    
    // Join worker threads
    for (auto& t : worker_threads) {
        if (t.joinable()) t.join();
    }

    // Print final stats
    std::cout << "\n=== FINAL STATISTICS ===" << std::endl;
    for (auto& e : engines) {
        auto stats = e->get_stats();
        std::cout << e->symbol() << ": matched=" << stats.orders_matched
                  << ", rejected=" << stats.orders_rejected
                  << ", partial=" << stats.orders_partially_filled
                  << std::endl;
    }

    return 0;
}