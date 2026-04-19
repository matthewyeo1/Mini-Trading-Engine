#include "velox/book/price_level.hpp"
#include <algorithm>

namespace velox {

PriceLevel::PriceLevel(int64_t price) : m_price(price) {}

void PriceLevel::add_order(Order* order) {
    order->prev = nullptr;
    order->next = m_head;
    
    if (m_head) {
        m_head->prev = order;
    } else {
        m_tail = order;
    }
    m_head = order;
    
    m_total_quantity.fetch_add(order->remaining_quantity, std::memory_order_release);
}

void PriceLevel::remove_order(Order* order) {
    if (order->prev) {
        order->prev->next = order->next;
    } else {
        m_head = order->next;
    }
    
    if (order->next) {
        order->next->prev = order->prev;
    } else {
        m_tail = order->prev;
    }
    
    m_total_quantity.fetch_sub(order->remaining_quantity, std::memory_order_release);
}

Order* PriceLevel::match_order(Order* incoming_order) {
    Order* current = m_head;
    
    while (current && incoming_order->remaining_quantity > 0) {
        uint32_t fill_qty = std::min(current->remaining_quantity, 
                                      incoming_order->remaining_quantity);
        
        if (fill_qty > 0) {
            current->fill(fill_qty);
            incoming_order->fill(fill_qty);
            
            // Check if current order is fully filled
            if (current->is_filled()) {
                Order* next = current->next;
                remove_order(current);
                current = next;
            } else {
                current = current->next;
            }
        } else {
            current = current->next;
        }
    }
    
    return (incoming_order->remaining_quantity > 0) ? nullptr : incoming_order;
}

}