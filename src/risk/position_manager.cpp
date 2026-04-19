#include "velox/risk/position_manager.hpp"
#include <cstring>

namespace velox {

PositionManager::PositionManager() = default;
PositionManager::~PositionManager() = default;

void PositionManager::update_position(const Order* order, uint32_t fill_quantity) {
    if (!order) return;
    
    uint32_t idx = hash_symbol(order->symbol);
    auto& pos = m_positions[idx];
    
    if (order->is_buy()) {
        int64_t new_pos = pos.net_position.load() + static_cast<int64_t>(fill_quantity);
        pos.net_position.store(new_pos, std::memory_order_release);
        pos.total_bought.fetch_add(fill_quantity, std::memory_order_release);
        update_average_price(pos, fill_quantity, order->price);
    } else {
        int64_t new_pos = pos.net_position.load() - static_cast<int64_t>(fill_quantity);
        pos.net_position.store(new_pos, std::memory_order_release);
        pos.total_sold.fetch_add(fill_quantity, std::memory_order_release);
    }
}

int64_t PositionManager::get_position(const char* symbol) const {
    uint32_t idx = hash_symbol(symbol);
    return m_positions[idx].net_position.load(std::memory_order_acquire);
}

void PositionManager::record_fill(const Order* order, uint32_t fill_quantity, int64_t fill_price) {
    update_position(order, fill_quantity);
    // TODO: Calculate P&L
}

int64_t PositionManager::get_realized_pnl() const {
    return m_total_realized_pnl.load(std::memory_order_acquire);
}

int64_t PositionManager::get_unrealized_pnl(int64_t current_price) const {
    // TODO: Calculate unrealized P&L based on positions
    return 0;
}

void PositionManager::reset() {
    for (auto& pos : m_positions) {
        pos.net_position.store(0, std::memory_order_release);
        pos.realized_pnl.store(0, std::memory_order_release);
        pos.avg_entry_price.store(0, std::memory_order_release);
        pos.total_bought.store(0, std::memory_order_release);
        pos.total_sold.store(0, std::memory_order_release);
    }
    m_total_realized_pnl.store(0, std::memory_order_release);
}

uint32_t PositionManager::hash_symbol(const char* symbol) const {
    uint32_t hash = 0;
    while (*symbol) {
        hash = hash * 31 + static_cast<uint32_t>(*symbol++);
    }
    return hash % 256;
}

void PositionManager::update_average_price(Position& pos, uint32_t quantity, int64_t price) {
    int64_t total_bought = pos.total_bought.load();
    int64_t old_avg = pos.avg_entry_price.load();
    
    if (total_bought == 0) {
        pos.avg_entry_price.store(price, std::memory_order_release);
    } else {
        int64_t new_avg = (old_avg * total_bought + price * quantity) / (total_bought + quantity);
        pos.avg_entry_price.store(new_avg, std::memory_order_release);
    }
}

}