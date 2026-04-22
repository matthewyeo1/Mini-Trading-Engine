#include "velox/book/price_level.hpp"
#include <algorithm>

namespace velox {

PriceLevel::PriceLevel(int64_t price)
    : m_price(price) {}

void PriceLevel::add_order(Order* order) {
    if (!order || order->remaining_quantity == 0) return;

    order->next = nullptr;
    order->prev = nullptr;

    if (m_tail) {
        m_tail->next = order;
    } else {
        m_head = order;
    }

    m_tail = order;

    m_size++;
    m_total_quantity.fetch_add(order->remaining_quantity, std::memory_order_relaxed);
}

void PriceLevel::remove_order(Order* order) {
    if (!order) return;

    if (!order->is_active()) return;

    order->cancel();

    m_total_quantity.fetch_sub(order->remaining_quantity,
                               std::memory_order_relaxed);
}

Order* PriceLevel::match_order(Order* incoming) {
    if (!incoming) return nullptr;

    Order* current = m_head;
    Order* last_valid = nullptr;

    while (current && incoming->remaining_quantity > 0) {

        // Skip cancelled or empty nodes
        if (!current->is_active() || current->remaining_quantity == 0) {
            current = current->next;
            continue;
        }

        uint32_t fill = std::min(current->remaining_quantity,
                                  incoming->remaining_quantity);

        if (fill > 0) {
            current->remaining_quantity -= fill;
            incoming->remaining_quantity -= fill;

            current->filled_quantity += fill;
            incoming->filled_quantity += fill;

            m_total_quantity.fetch_sub(fill, std::memory_order_relaxed);
        }

        if (current->remaining_quantity == 0) {
            Order* next = current->next;

            if (current->prev) {
                current->prev->next = current->next;
            } else {
                m_head = current->next;
            }

            if (current->next) {
                current->next->prev = current->prev;
            } else {
                m_tail = current->prev;
            }

            current->next = nullptr;
            current->prev = nullptr;

            m_size--;

            current = next;
            continue;
        }

        last_valid = current;
        current = current->next;
    }

    // clean head pointer if it drifted onto inactive nodes
    while (m_head && (!m_head->is_active() || m_head->remaining_quantity == 0)) {
        m_head = m_head->next;
    }

    return (incoming->remaining_quantity > 0) ? incoming : nullptr;
}

}