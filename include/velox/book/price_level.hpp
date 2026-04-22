#pragma once
#include <atomic>
#include <cstdint>
#include "velox/matching/order.hpp"

namespace velox {

class PriceLevel {
public:
    explicit PriceLevel(int64_t price);
    ~PriceLevel() = default;
    
    // Order management
    void add_order(Order* order);
    void remove_order(Order* order);
    Order* match_order(Order* incoming_order);
    
    // Getters
    int64_t price() const { return m_price; }
    uint32_t total_quantity() const { return m_total_quantity.load(std::memory_order_relaxed); }
    bool empty() const { return m_head == nullptr; }
    

    Order* head() const { return m_head; }
    Order* tail() const { return m_tail; } 
    
private:
    int64_t m_price;

    std::atomic<uint32_t> m_total_quantity{0};
    int64_t m_size = 0;

    Order* m_head = nullptr;
    Order* m_tail = nullptr;
};

}