#include <iostream>
#include <fstream>
#include <vector>
#include <chrono>
#include <thread>
#include "velox/feed/feed_handler.hpp"
#include "velox/core/symbol_engine.hpp"
#include "velox/risk/risk_manager.hpp"
#include "velox/gateway/execution_gateway.hpp"
#include "velox/risk/position_manager.hpp"

using namespace velox;

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <ITCH_file.bin> [speed_factor]\n";
        return 1;
    }

    const char* filename = argv[1];
    double speed = (argc >= 3) ? std::stod(argv[2]) : 1.0;

    std::ifstream file(filename, std::ios::binary);
    if (!file) {
        std::cerr << "Failed to open: " << filename << std::endl;
        return 1;
    }

    // Engine setup
    RiskManager risk;
    ExecutionGateway gateway;
    PositionManager pos_mgr;
    FeedHandler feed;

    const int num_workers = std::thread::hardware_concurrency();
    for (int i = 0; i < num_workers; ++i) {
        gateway.add_worker();
    }

    std::vector<std::unique_ptr<SymbolEngine>> engines;
    engines.push_back(std::make_unique<SymbolEngine>("AAPL", &risk, &gateway, &pos_mgr));
    engines.push_back(std::make_unique<SymbolEngine>("MSFT", &risk, &gateway, &pos_mgr));
    engines.push_back(std::make_unique<SymbolEngine>("GOOGL", &risk, &gateway, &pos_mgr));
    engines.push_back(std::make_unique<SymbolEngine>("AMZN", &risk, &gateway, &pos_mgr));
    engines.push_back(std::make_unique<SymbolEngine>("META", &risk, &gateway, &pos_mgr));

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

    std::cout << "Starting at offset: " << file.tellg() << std::endl;
    std::cout << "Replaying " << filename << " at " << speed << "x speed\n";

    // Read entire file in chunks and feed to FeedHandler
    // FeedHandler handles all ITCH parsing internally
    const size_t BUFFER_SIZE = 64 * 1024;  // 64KB chunks
    std::vector<char> buffer(BUFFER_SIZE);
    uint64_t total_bytes = 0;
    
    // Optional: track time for speed control
    auto start_time = std::chrono::steady_clock::now();
    uint64_t bytes_processed = 0;

    while (file.read(buffer.data(), BUFFER_SIZE) || file.gcount() > 0) {
        size_t bytes_read = file.gcount();
        total_bytes += bytes_read;
        
        // Optional: add speed control here if needed (requires timestamp extraction)
        
        // Feed the raw data to FeedHandler – it handles all ITCH parsing
        feed.process(buffer.data(), bytes_read);
        
        // Progress indicator
        if (total_bytes / (1024 * 1024) > bytes_processed / (1024 * 1024) + 100) {
            bytes_processed = total_bytes;
            std::cout << "\rProcessed " << total_bytes / (1024 * 1024) << " MB..." << std::flush;
        }
    }

    file.close();
    std::cout << "\nFile read complete. Draining orders...\n";

    // Drain remaining orders
    for (auto& e : engines) {
        e->run_match_cycle();
    }

    // Print final stats
    std::cout << "\n=== FINAL STATS ===\n";
    for (auto& e : engines) {
        auto stats = e->get_stats();
        int64_t pnl = pos_mgr.get_realized_pnl(e->symbol());
        std::cout << e->symbol() << ": matched=" << stats.orders_matched
                  << ", partial=" << stats.orders_partially_filled
                  << ", P&L=$" << (pnl / 100.0) << std::endl;
    }

    return 0;
}