#include "velox/risk/position_manager.hpp"
#include <cstring>
#include <cmath>
#include <iostream>

namespace velox {

PositionManager::PositionManager() = default;
PositionManager::~PositionManager() = default;

void PositionManager::update_position(const Order* order, uint32_t fill_quantity, int64_t fill_price) {
    if (!order || fill_quantity == 0) return;
    
    // Trim trailing spaces from symbol
    std::string symbol(order->symbol);
    while (!symbol.empty() && symbol.back() == ' ') {
        symbol.pop_back();
    }
    
    // std::cout << "[POSITION] Symbol='" << symbol << "' (original='" << order->symbol << "')" << std::endl;

    // Create default position if symbol doesn't exist in system
    auto& pos = m_positions[symbol];

    if (order->is_buy()) {
        /*
        std::cout << "[POSITION] BUY: order_id=" << order->order_id 
              << " fill_price=" << fill_price 
              << " order.price=" << order->price << std::endl;
        */

        // Buy: increase position, update average entry price
        int64_t old_position = pos.net_position.load(std::memory_order_acquire);
        int64_t old_avg = pos.avg_entry_price.load(std::memory_order_acquire);
        uint64_t old_bought = pos.total_bought.load(std::memory_order_acquire);
        
        int64_t new_position = old_position + fill_quantity;
        uint64_t new_bought = old_bought + fill_quantity;
        
        
        int64_t new_avg;
        if (old_bought == 0) {
            // First fill: use the order's own price
            new_avg = fill_price; 
        } else {
            new_avg = (old_avg * old_bought + fill_price * fill_quantity) / new_bought;
        }
        
        pos.net_position.store(new_position, std::memory_order_release);
        pos.avg_entry_price.store(new_avg, std::memory_order_release);
        pos.total_bought.store(new_bought, std::memory_order_release);
    }
    else {
        // Sell: decrease position, realize P&L
        int64_t old_position = pos.net_position.load(std::memory_order_acquire);
        int64_t old_avg = pos.avg_entry_price.load(std::memory_order_acquire);
        
        /*
        std::cout << "[POSITION] Sell: fill_price=" << fill_price 
              << " old_avg=" << old_avg 
              << " quantity=" << fill_quantity << std::endl;
        */

        // Realized P&L = (sell_price - avg_price) * quantity
        int64_t pnl = (fill_price - old_avg) * fill_quantity;

        // std::cout << "[POSITION] P&L=" << pnl << std::endl;

        update_realized_pnl(pos, pnl);
        
        int64_t new_position = old_position - fill_quantity;
        pos.net_position.store(new_position, std::memory_order_release);
        pos.total_sold.fetch_add(fill_quantity, std::memory_order_release);
    }
}

void PositionManager::update_realized_pnl(Position& pos, int64_t pnl) {
    pos.realized_pnl.fetch_add(pnl, std::memory_order_release);
}

int64_t PositionManager::get_position(const char* symbol) const {
    std::string sym(symbol);
    while (!sym.empty() && sym.back() == ' ') sym.pop_back();

    auto it = m_positions.find(symbol);
    if (it == m_positions.end()) return 0;
    return it->second.net_position.load(std::memory_order_acquire);
}

int64_t PositionManager::get_realized_pnl(const char* symbol) const {
    std::string sym(symbol);
    while (!sym.empty() && sym.back() == ' ') sym.pop_back();

    auto it = m_positions.find(symbol);
    if (it == m_positions.end()) return 0;
    return it->second.realized_pnl.load(std::memory_order_acquire);
}

int64_t PositionManager::get_unrealized_pnl(const char* symbol, int64_t current_price) const {
    std::string sym(symbol);
    while (!sym.empty() && sym.back() == ' ') sym.pop_back();

    auto it = m_positions.find(symbol);
    if (it == m_positions.end()) return 0;
    
    int64_t position = it->second.net_position.load(std::memory_order_acquire);
    if (position == 0) return 0;
    
    int64_t avg_price = it->second.avg_entry_price.load(std::memory_order_acquire);
    if (position > 0) {
        // Long position: (current - avg) * position
        return (current_price - avg_price) * position;
    } else {
        // Short position: (avg - current) * (-position)
        return (avg_price - current_price) * (-position);
    }
}

int64_t PositionManager::get_total_pnl(const char* symbol, int64_t current_price) const {
    std::string sym(symbol);
    while (!sym.empty() && sym.back() == ' ') sym.pop_back();

    return get_realized_pnl(symbol) + get_unrealized_pnl(symbol, current_price);
}

void PositionManager::reset() {
    m_positions.clear();
}

}