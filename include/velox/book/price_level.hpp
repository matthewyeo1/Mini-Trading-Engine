#pragma once
#include <cstdint>
#include <array>
#include "velox/matching/order.hpp"

namespace velox {

class PriceLevel {
public:
    explicit PriceLevel(int64_t price);

    // Fixed ring buffer size (must be power of 2)
    static constexpr size_t MAX_ORDERS = 128;
    static constexpr size_t MASK = MAX_ORDERS - 1;

    // Core operations
    void add_order(Order* order);
    void remove_order(Order* order);        
    Order* match_order(Order* incoming);

    // Getters
    int64_t price() const { return m_price; }
    uint32_t total_quantity() const { return m_total_quantity; }
    bool empty() const { return m_size == 0; }

    // Buffer operations
    Order* head() const;
    Order* tail() const;
    void advance();

private:
    int64_t m_price;

    std::array<Order*, MAX_ORDERS> m_buffer{};

    size_t m_head = 0;
    size_t m_tail = 0;

    uint32_t m_total_quantity = 0;
    size_t m_size = 0;

    inline size_t next(size_t i) const { return (i + 1) & MASK; }
};

}