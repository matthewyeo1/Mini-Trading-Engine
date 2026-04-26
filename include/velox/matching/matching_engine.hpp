#pragma once
#include <atomic>
#include "velox/book/order_book.hpp"
#include "lockfree/spsc_queue.hpp"
#include "lockfree/pool.hpp"
#include "velox/risk/risk_manager.hpp"
#include "velox/gateway/execution_gateway.hpp"
#include "velox/risk/position_manager.hpp"
#include "velox/matching/order.hpp"

namespace velox {

class MatchingEngine {
public:
    using OrderQueue = lockfree::SPSCQueue<Order*, 65536>;
    
    MatchingEngine(const char* symbol,
                   RiskManager* risk_manager,
                   ExecutionGateway* gateway,
                   PositionManager* position_manager);

    ~MatchingEngine();
    
    // Submit order (called by Risk Manager)
    bool submit_order(Order* order);

    // Cancel order (via Order Book)
    bool cancel_order(uint64_t order_id) {
        return m_order_book.cancel_order(order_id);
    }
    
    // Run matching cycle (called by Matching Engine)
    void run_match_cycle();
    
    // Stats
    struct Stats {
        uint64_t orders_matched = 0;
        uint64_t orders_rejected = 0;
        uint64_t orders_partially_filled = 0;
        uint64_t avg_match_latency_ns = 0;
    };
    
    Stats get_stats() const;

private:
    OrderBook m_order_book;
    RiskManager* m_risk_manager;
    ExecutionGateway* m_gateway;
    PositionManager* m_position_manager;

    OrderQueue m_incoming_orders;

    alignas(64) std::atomic<uint64_t> m_match_count{0};
    alignas(64) std::atomic<uint64_t> m_reject_count{0};
    alignas(64) std::atomic<uint64_t> m_partial_count{0};
    alignas(64) std::atomic<uint64_t> m_total_latency{0};
    
    void process_order(Order* order);
    void send_fill(Order* order, uint32_t fill_quantity, int64_t fill_price);
    bool check_risk(const Order* order) const;
};

}