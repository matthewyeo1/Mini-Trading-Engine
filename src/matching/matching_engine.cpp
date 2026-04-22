#include "velox/matching/matching_engine.hpp"
#include <cstring>

namespace velox {

MatchingEngine::MatchingEngine(const char* symbol,
                               RiskManager* risk_manager,
                               ExecutionGateway* gateway)
    : m_order_book(symbol),
      m_risk_manager(risk_manager),
      m_gateway(gateway) {}

MatchingEngine::~MatchingEngine() = default;

bool MatchingEngine::submit_order(Order* order) {
    if (!order) return false;

    return m_incoming_orders.push(order);
}

void MatchingEngine::run_match_cycle() {
    while (auto opt_order = m_incoming_orders.pop()) {
        process_order(opt_order.value());
    }
}

void MatchingEngine::process_order(Order* order) {

    // Run risk check
    if (!check_risk(order)) {
        order->status = OrderStatus::REJECTED;
        m_reject_count++;

        // Send rejection report
        ExecutionReport report;
        report.order_id = order->order_id;
        report.client_order_id = order->client_order_id;
        std::strncpy(report.symbol, order->symbol, 7);
        report.status = OrderStatus::REJECTED;
        report.reject_reason = "Risk limit exceeded";
        m_gateway->send_report(report);
        return;

    }

    // Match against order book
    Order* remaining = m_order_book.match(order);

    // Send fills for executed portion
    if (order->filled_quantity > 0) {
        send_fill(order, order->filled_quantity, order->price);
        m_match_count++;
    }

    // If partially filled, remainder is added to the book
    if (remaining && remaining->remaining_quantity > 0) {
        m_partial_count++;
    }

    // If fully filled, order is done
    if (order->is_filled()) {

    } 
}

void MatchingEngine::send_fill(Order* order, uint32_t fill_quantity, int64_t fill_price) {
    (void)fill_quantity;
    (void)fill_price;

    m_gateway->send_order(order);
}

bool MatchingEngine::check_risk(const Order* order) const {
    if (!m_risk_manager) return true;

    return m_risk_manager->check_order(order);
}

MatchingEngine::Stats MatchingEngine::get_stats() const {
    Stats stats;
    stats.orders_matched = m_match_count.load(std::memory_order_acquire);
    stats.orders_rejected = m_reject_count.load(std::memory_order_acquire);
    stats.orders_partially_filled = m_partial_count.load(std::memory_order_acquire);

    return stats;
}

}