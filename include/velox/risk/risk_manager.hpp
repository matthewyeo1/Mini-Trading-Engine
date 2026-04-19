#pragma once
#include <atomic>
#include <cstdint>
#include "velox/matching/order.hpp"

namespace velox {

class RiskManager {
public:
    RiskManager();
    ~RiskManager();
    
    // Risk checks
    bool check_order(const Order* order) const;
    bool check_position(const Order* order, uint32_t fill_quantity) const;
    
    // Position limits
    void set_position_limit(const char* symbol, uint32_t limit);
    uint32_t current_position(const char* symbol) const;
    
    // Circuit breakers
    bool is_circuit_breaker_active() const;
    void activate_circuit_breaker();
    void reset_circuit_breaker();
    
private:
    struct Position {
        std::atomic<uint32_t> long_position{0};
        std::atomic<uint32_t> short_position{0};
        uint32_t limit = 100000;  // Default limit
    };
    
    // Simplified - would be hash map in production
    Position m_positions[256];
    std::atomic<bool> m_circuit_breaker{false};
    
    uint32_t hash_symbol(const char* symbol) const;
};

} 