#include "velox/book/price_level.hpp"
#include <algorithm>

namespace velox {

PriceLevel::PriceLevel(int64_t price)
    : m_price(price) {}

void PriceLevel::add_order(Order* order) {
    if (!order || order->remaining_quantity == 0) return;

    order->next = nullptr;
    order->prev = nullptr;

    if (!m_tail) {
        m_head = m_tail = order;
    } else {
        m_tail->next = order;
        order->prev = m_tail;
        m_tail = order;
    }

    m_size++;
    m_total_quantity.fetch_add(order->remaining_quantity, std::memory_order_relaxed);
}

void PriceLevel::remove_order(Order* order) {
    if (!order) return;

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

    order->next = nullptr;
    order->prev = nullptr;

    m_total_quantity.fetch_sub(order->remaining_quantity,
                               std::memory_order_relaxed);
}

Order* PriceLevel::match_order(Order* incoming) {
    if (!incoming) return nullptr;

    Order* current = m_head;

    while (current && incoming->remaining_quantity > 0) {

        uint32_t fill = std::min(current->remaining_quantity,
                                  incoming->remaining_quantity);

        if (fill == 0) {
            current = current->next;
            continue;
        }

        // Apply fills symmetrically
        current->remaining_quantity -= fill;
        incoming->remaining_quantity -= fill;

        current->filled_quantity += fill;
        incoming->filled_quantity += fill;

        m_total_quantity.fetch_sub(fill, std::memory_order_relaxed);

        // Remove fully filled maker
        if (current->remaining_quantity == 0) {
            Order* next = current->next;
            remove_order(current);
            current = next;
        }

        // Stop if incoming is done
        if (incoming->remaining_quantity == 0) {
            break;
        }
    }

    return (incoming->remaining_quantity > 0) ? incoming : nullptr;
}

}