#pragma once
#include "velox/book/order_book.hpp"
#include "velox/matching/matching_engine.hpp"
#include "velox/feed/feed_handler.hpp"
#include "lockfree/spsc_queue.hpp"

namespace velox {

class SymbolEngine {
public:
    SymbolEngine(const char* symbol, 
                RiskManager* risk_manager,
                ExecutionGateway* gateway,
                PositionManager* position_manager)
        : m_book(symbol),
          m_engine(symbol, risk_manager, gateway, position_manager) {}

    // Called by Feed Handler thread to push an order from ITCH
    void on_market_order(Order* order) {
        m_engine.submit_order(order);
    }

    // Called by Matching Engine thread to run matching
    void run_match_cycle() {
        m_engine.run_match_cycle();
    }

    // Cancel order (via Order Book)
    bool cancel_order(uint64_t order_id) {
        return m_engine.cancel_order(order_id);
    }

    // Access to Order Book for snapshots
    const OrderBook& order_book() const {
        return m_book;
    }

    // Get stats from Matching Engine thread
    MatchingEngine::Stats get_stats() { return m_engine.get_stats(); }

    // Get symbol
    const char* symbol() { return m_book.symbol(); }

private:
        OrderBook m_book;
        MatchingEngine m_engine;

};

}