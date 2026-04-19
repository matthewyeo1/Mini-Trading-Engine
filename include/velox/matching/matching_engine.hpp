#pragma once
#include <atomic>
#include "velox/book/order_book.hpp"
#include "velox/matching/order.hpp"
#include "lockfree/spsc_queue.hpp"
#include "lockfree/pool.hpp"

namespace velox {

class MatchingEngine {
public:
    using OrderQueue = lockfree::SPSCQueue<Order*, 65536>;
    
    MatchingEngine();
    ~MatchingEngine();
    
    // Submit order (called by risk manager)
    bool submit_order(Order* order);
    
    // Run matching cycle (called by matching thread)
    void run_match_cycle();
    
    // Statistics
    struct Stats {
        uint64_t orders_matched = 0;
        uint64_t orders_rejected = 0;
        uint64_t avg_match_latency_ns = 0;
    };
    
    Stats get_stats() const;
    
private:
    OrderQueue m_incoming_orders;
    std::atomic<uint64_t> m_match_count{0};
    std::atomic<uint64_t> m_total_latency{0};
    
    void process_order(Order* order);
    void execute_match(Order* buy, Order* sell, uint32_t quantity);
    void send_fill(Order* order, uint32_t fill_quantity, int64_t fill_price);
};

}