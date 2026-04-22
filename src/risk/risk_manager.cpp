#include "velox/risk/risk_manager.hpp"
#include <cstring>

namespace velox {

RiskManager::RiskManager() = default;
RiskManager::~RiskManager() = default;

// To be called by matching engine before matching order
bool RiskManager::check_order(const Order* order) const {
    if (!order) return false;
    
    // Check circuit breaker
    if (is_circuit_breaker_active()) return false;
    
    // Basic validation
    if (order->quantity == 0) return false;
    if (order->price < 0) return false;
    
    // Check position limits
    return check_position(order, order->quantity);
}

bool RiskManager::check_position(const Order* order, uint32_t fill_quantity) const {
    if (!order) return false;
    
    uint32_t idx = hash_symbol(order->symbol);
    const auto& pos = m_positions[idx];
    
    // Check if it's a buy or sell order
    if (order->is_buy()) {
        // Check buy limit
        uint32_t new_long = pos.long_position.load(std::memory_order_acquire) + fill_quantity;
        uint32_t new_short = pos.short_position.load(std::memory_order_acquire);
        return (new_long - new_short) <= pos.limit;
    } else {
        // Check sell limit
        uint32_t new_short = pos.short_position.load(std::memory_order_acquire) + fill_quantity;
        uint32_t new_long = pos.long_position.load(std::memory_order_acquire);
        return (new_long - new_short) >= -static_cast<int64_t>(pos.limit);
    }
}

void RiskManager::set_position_limit(const char* symbol, uint32_t limit) {
    uint32_t idx = hash_symbol(symbol);
    m_positions[idx].limit = limit;
}

uint32_t RiskManager::current_position(const char* symbol) const {
    uint32_t idx = hash_symbol(symbol);
    const auto& pos = m_positions[idx];
    int64_t long_pos = pos.long_position.load(std::memory_order_acquire);
    int64_t short_pos = pos.short_position.load(std::memory_order_acquire);
    return static_cast<uint32_t>(long_pos - short_pos);
}

bool RiskManager::is_circuit_breaker_active() const {
    return m_circuit_breaker.load(std::memory_order_acquire);
}

void RiskManager::activate_circuit_breaker() {
    m_circuit_breaker.store(true, std::memory_order_release);
}

void RiskManager::reset_circuit_breaker() {
    m_circuit_breaker.store(false, std::memory_order_release);
}

void RiskManager::update_position(const Order* order, uint32_t fill_quantity) {
    uint32_t idx = hash_symbol(order->symbol);
    auto& pos = m_positions[idx];
    
    if (order->is_buy()) {
        pos.long_position.fetch_add(fill_quantity, std::memory_order_release);
    } else {
        pos.short_position.fetch_add(fill_quantity, std::memory_order_release);
    }
}

uint32_t RiskManager::hash_symbol(const char* symbol) const {
    uint32_t hash = 0;
    while (*symbol) {
        hash = hash * 31 + static_cast<uint32_t>(*symbol++);
    }
    return hash % 256;
}

}