#pragma once
#include <atomic>
#include <cstdint>
#include "velox/matching/order.hpp"

namespace velox {

class PositionManager {
public:
    PositionManager();
    ~PositionManager();
    
    // Update position after fill
    void update_position(const Order* order, uint32_t fill_quantity);
    
    // Get current position for symbol
    int64_t get_position(const char* symbol) const;
    
    // P&L tracking
    void record_fill(const Order* order, uint32_t fill_quantity, int64_t fill_price);
    int64_t get_realized_pnl() const;
    int64_t get_unrealized_pnl(int64_t current_price) const;
    
    // Reset for testing
    void reset();
    
private:
    struct Position {
        std::atomic<int64_t> net_position{0};
        std::atomic<int64_t> realized_pnl{0};
        std::atomic<int64_t> avg_entry_price{0};
        std::atomic<uint32_t> total_bought{0};
        std::atomic<uint32_t> total_sold{0};
    };
    
    Position m_positions[256];
    std::atomic<int64_t> m_total_realized_pnl{0};
    
    uint32_t hash_symbol(const char* symbol) const;
    void update_average_price(Position& pos, uint32_t quantity, int64_t price);
};

} 