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
        std::cerr << "  speed_factor: 1.0 = real time, 2.0 = 2x faster, 0.5 = half speed\n";
        return 1;
    }

    const char* filename = argv[1];
    double speed = (argc >= 3) ? std::stod(argv[2]) : 1.0;

    // Open ITCH binary file
    std::ifstream file(filename, std::ios::binary);
    if (!file) {
        std::cerr << "Failed to open: " << filename << std::endl;
        return 1;
    }

    // Engine setup (same as main.cpp)
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

    // Replay loop
    std::cout << "Replaying " << filename << " at " << speed << "x speed\n";

    // Initialize buffer
    std::vector<char> buffer(65536);  
    uint64_t prev_timestamp = 0;
    auto start_real = std::chrono::steady_clock::now();

    while (file) {
        // Read next message header
        uint8_t header[2];
        if (!file.read(reinterpret_cast<char*>(header), 2)) break;
        uint16_t msg_len = (static_cast<uint16_t>(header[0]) << 8) | header[1];
        if (msg_len < 2) break;

        // Read full message
        std::vector<uint8_t> msg(msg_len);
        msg[0] = header[0];
        msg[1] = header[1];
        if (!file.read(reinterpret_cast<char*>(msg.data() + 2), msg_len - 2)) break;

        // Extract timestamp (nanoseconds since midnight)
        uint64_t timestamp = 0;

        // All ITCH messages have 8-byte timestamp at offset 3
        if (msg_len >= 11) {  
            for (int i = 0; i < 8; ++i) {
                timestamp = (timestamp << 8) | msg[3 + i];
            }
        }

        // Simulate real-time arrival
        if (prev_timestamp != 0 && timestamp > prev_timestamp) {
            uint64_t delta_ns = static_cast<uint64_t>((timestamp - prev_timestamp) / speed);
            std::this_thread::sleep_for(std::chrono::nanoseconds(delta_ns));
        }
        prev_timestamp = timestamp;

        // Feed message into parser
        feed.process(reinterpret_cast<const char*>(msg.data()), msg.size());
    }

    file.close();

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
                  << ", P&L=$" << (pnl / 100.0) << "\n";
    }

    return 0;
}