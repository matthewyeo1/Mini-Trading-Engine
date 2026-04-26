#include "velox/matching/matching_engine.hpp"
#include <cstring>
#include <iostream>

namespace velox {

MatchingEngine::MatchingEngine(const char* symbol,
                               RiskManager* risk_manager,
                               ExecutionGateway* gateway,
                               PositionManager* positiion_manager)
    : m_order_book(symbol),
      m_risk_manager(risk_manager),
      m_gateway(gateway),
      m_position_manager(positiion_manager) {}

MatchingEngine::~MatchingEngine() = default;

bool MatchingEngine::submit_order(Order* order) {
    if (!order) return false;

    // std::cout << "[DEBUG] MatchingEngine::submit_order, pushing to queue" << std::endl;
    return m_incoming_orders.push(order);
}

void MatchingEngine::run_match_cycle() {
    // std::cout << "[DEBUG] MatchingEngine::run_match_cycle" << std::endl;
    
    while (auto opt_order = m_incoming_orders.pop()) {
        process_order(opt_order.value());
    }
}

void MatchingEngine::process_order(Order* order) {
    /*
    std::cout << "[MATCHING_ENGINE] process_order ID=" << order->order_id 
              << " side=" << (order->is_buy() ? "BUY" : "SELL")
              << " price=" << order->price << std::endl;
    */

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
    
    // Convert market orders to aggressive limit orders
    if (order->type == OrderType::MARKET) {
        if (order->is_buy()) {
            order->price = INT64_MAX;
        } else {
            order->price = 0;
        }

        order->type = OrderType::LIMIT;
    }
    
    std::vector<Fill> fills;
    fills.reserve(16);   

    // Match against order book
    Order* remaining = m_order_book.match(order, fills);

    // Update position for every resting order that was filled
    for (const Fill& fill : fills) {
        /*
        std::cout << "[PROCESS_ORDER] Resting order filled. ID=" << fill.order->order_id 
              << " qty=" << fill.quantity 
              << " fill.price=" << fill.price 
              << " order.price=" << fill.order->price << std::endl;
        */

        if (m_position_manager) {
            m_position_manager->update_position(fill.order, fill.quantity, fill.order->price);
        }
    }

    // Send fills for executed portion
    if (order->filled_quantity > 0) {
        send_fill(order, order->filled_quantity, order->price);

        // Count per fill
        m_match_count += fills.empty() ? 1 : fills.size();
    }

    // Partial count only incremented when a fill actually occurred
    if (order->filled_quantity > 0 && order->remaining_quantity > 0) {
        m_partial_count++;
    }

    // Finalize order state AFTER matching
    if (order->remaining_quantity == 0) {
        order->status = OrderStatus::FILLED;
    } 
    else if (order->filled_quantity > 0) {
        order->status = OrderStatus::PARTIAL;
    } 
    else {
        order->status = OrderStatus::NEW;
    }
}

void MatchingEngine::send_fill(Order* order, uint32_t fill_quantity, int64_t fill_price) {
    (void)fill_quantity;
    (void)fill_price;

    if (m_gateway) {
        m_gateway->send_order(order);
    }

    // Update position manager
    if (m_position_manager) {
        m_position_manager->update_position(order, fill_quantity, fill_price);
    }
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