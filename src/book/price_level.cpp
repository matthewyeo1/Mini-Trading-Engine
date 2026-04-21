#include "velox/book/price_level.hpp"
#include <algorithm>

namespace velox {

PriceLevel::PriceLevel(int64_t price) : m_price(price) {}

void PriceLevel::add_order(Order* order) {
    if (!order) return;

    order->next = nullptr;
    order->prev = m_tail;

    if (m_tail) {
        m_tail->next = order;
    } else {
        m_head = order;  // first order
    }
    m_tail = order;

    m_total_quantity.fetch_add(order->remaining_quantity, std::memory_order_release);
}

void PriceLevel::remove_order(Order* order) {
    if (!order) return;

    // Remove order from linked list
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

    // Clear order pointers for safety
    order->prev = nullptr;
    order->next = nullptr;
}

Order* PriceLevel::match_order(Order* incoming_order) {
    if (!incoming_order || incoming_order->remaining_quantity == 0) {
        return nullptr;
    }

    Order* current = m_head;
    
    while (current && incoming_order->remaining_quantity > 0) {
        uint32_t fill_qty = std::min(current->remaining_quantity, 
                                      incoming_order->remaining_quantity);
        
        if (fill_qty > 0) {
            current->fill(fill_qty);
            incoming_order->fill(fill_qty);
            // Decrease total quantity by the amount filled
            m_total_quantity.fetch_sub(fill_qty, std::memory_order_release);
            
            // Check if current order is fully filled for removal
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
    
    // Return remaining incoming order if not fully filled
    return (incoming_order->remaining_quantity > 0) ? incoming_order : nullptr;
}