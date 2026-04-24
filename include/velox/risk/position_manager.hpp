#pragma once
#include <atomic>
#include <cstdint>
#include <unordered_map>
#include <string>
#include "velox/matching/order.hpp"

namespace velox {

struct Position {
    std::atomic<int64_t> net_position{0};       // Total bought - total sold
    std::atomic<int64_t> realized_pnl{0};       // Locked-in P&L
    std::atomic<int64_t> avg_entry_price{0};    // Average entry price 
    std::atomic<uint32_t> total_bought{0};      
    std::atomic<uint32_t> total_sold{0};
};

class PositionManager {
public:
    PositionManager();
    ~PositionManager();
    
    // Update position after fill (called by Execution Gateway)
    void update_position(const Order* order, uint32_t fill_quantity, int64_t fill_price);
    
    // Get current position for symbol
    int64_t get_position(const char* symbol) const;
    
    // P&L tracking
    int64_t get_realized_pnl(const char* symbol) const;
    int64_t get_unrealized_pnl(const char* symbol, int64_t current_price) const;
    int64_t get_total_pnl(const char* symbol, int64_t current_price) const;
    
    // Reset for testing
    void reset();
    
private:
    std::unordered_map<std::string, Position> m_positions;
    void update_realized_pnl(Position& pos, int64_t price);
};

} 