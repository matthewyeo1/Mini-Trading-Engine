#pragma once
#include <atomic>
#include "velox/book/order_book.hpp"
#include "velox/matching/order.hpp"
#include "lockfree/spsc_queue.hpp"
#include "lockfree/pool.hpp"
#include "velox/risk/risk_manager.hpp"
#include "velox/gateway/execution_gateway.hpp"

namespace velox {

class MatchingEngine {
public:
    using OrderQueue = lockfree::SPSCQueue<Order*, 65536>;
    
    MatchingEngine(const char* symbol,
                   RiskManager* risk_manager,
                   ExecutionGateway* gateway);

    ~MatchingEngine();
    
    // Submit order (called by risk manager)
    bool submit_order(Order* order);
    
    // Run matching cycle (called by matching thread)
    void run_match_cycle();
    
    // Statistics
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

    OrderQueue m_incoming_orders;

    std::atomic<uint64_t> m_match_count{0};
    std::atomic<uint64_t> m_reject_count{0};
    std::atomic<uint64_t> m_partial_count{0};
    std::atomic<uint64_t> m_total_latency{0};
    
    void process_order(Order* order);
    void send_fill(Order* order, uint32_t fill_quantity, int64_t fill_price);
    bool check_risk(const Order* order) const;
};

}