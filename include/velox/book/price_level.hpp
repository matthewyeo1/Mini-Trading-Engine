#pragma once
#include <atomic>
#include "velox/matching/order.hpp"

namespace velox {

class PriceLevel {
public:
    explicit PriceLevel(int64_t price);
    
    // Order management
    void add_order(Order* order);
    void remove_order(Order* order);
    Order* match_order(Order* incoming_order);
    
    // Getters
    int64_t price() const { return m_price; }
    uint32_t total_quantity() const { return m_total_quantity.load(std::memory_order_acquire); }
    bool empty() const { return m_head == nullptr; }
    Order* head() const { return m_head; }
    
    // For hash map
    int64_t key() const { return m_price; }
    
private:
    int64_t m_price = 0;
    std::atomic<uint32_t> m_total_quantity{0};
    Order* m_head = nullptr;
    Order* m_tail = nullptr;
};

}