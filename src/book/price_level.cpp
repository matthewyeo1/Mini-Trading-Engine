#include "velox/book/price_level.hpp"
#include <algorithm>

namespace velox {

PriceLevel::PriceLevel(int64_t price)
    : m_price(price), m_head(0), m_tail(0),
      m_total_quantity(0), m_size(0)
{
    m_buffer.fill(nullptr);
}

void PriceLevel::add_order(Order* order) {
    if (!order || order->remaining_quantity == 0) return;

    m_buffer[m_tail] = order;
    m_tail = next(m_tail);

    m_size++;
    m_total_quantity += order->remaining_quantity;
}

void PriceLevel::remove_order(Order* order) {
    if (!order || !order->is_active()) return;

    order->cancel();
    m_total_quantity -= order->remaining_quantity;

    // Only scan if the cancelled order is blocking execution
    if (m_head != m_tail && m_buffer[m_head] == order) {
        advance();
    }
}

Order* PriceLevel::match_order(Order* incoming) {
    if (!incoming) return nullptr;

    while (incoming->remaining_quantity > 0) {

        advance();

        if (m_head == m_tail) break;

        Order* current = m_buffer[m_head];

        // Skip null or inactive (lazy deletion)
        if (!current || !current->is_active() || current->remaining_quantity == 0) {
            m_buffer[m_head] = nullptr;
            m_head = next(m_head);
            continue;
        }

        uint32_t fill = std::min(current->remaining_quantity,
                                incoming->remaining_quantity);

        // Apply fill
        current->remaining_quantity -= fill;
        incoming->remaining_quantity -= fill;

        current->filled_quantity += fill;
        incoming->filled_quantity += fill;

        m_total_quantity -= fill;

        // If maker fully filled → pop from FIFO
        if (current->remaining_quantity == 0) {
            m_buffer[m_head] = nullptr;
            m_head = next(m_head);
            m_size--;
        }

        if (incoming->remaining_quantity == 0)
            break;
    }

    return (incoming->remaining_quantity > 0) ? incoming : nullptr;
}

Order* PriceLevel::head() const {
    if (m_head == m_tail) return nullptr;
    return m_buffer[m_head];
}

Order* PriceLevel::tail() const {
    if (m_head == m_tail) return nullptr;
    return m_buffer[(m_tail - 1) & MASK];
}

void PriceLevel::advance() {
    while (m_head != m_tail) {
        Order* order = m_buffer[m_head];

        if (order && order->is_active() && order->remaining_quantity > 0) {
            return;
        }

        // Clean dead slot
        m_buffer[m_head] = nullptr;
        m_head = next(m_head);
        m_size--;
    }
}

}