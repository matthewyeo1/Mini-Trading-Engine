#pragma once
#include <atomic>
#include <cstdint>
#include "velox/matching/order.hpp"
#include "lockfree/spsc_queue.hpp"

namespace velox {

struct ExecutionReport {
    uint64_t order_id = 0;
    uint64_t client_order_id = 0;
    char symbol[8] = {0};
    OrderSide side = OrderSide::BUY;
    OrderStatus status = OrderStatus::NEW;
    uint32_t executed_quantity = 0;
    int64_t executed_price = 0;
    uint64_t timestamp = 0;
    const char* reject_reason = nullptr;
};

class ExecutionGateway {
public:
    using ReportQueue = lockfree::SPSCQueue<ExecutionReport*, 65536>;
    
    ExecutionGateway();
    ~ExecutionGateway();
    
    // Send order to exchange (simulated)
    bool send_order(const Order* order);
    
    // Cancel order
    bool cancel_order(uint64_t order_id);
    
    // Receive execution reports (consumer thread)
    ExecutionReport* receive_report();
    
    // Statistics
    uint64_t orders_sent() const { return m_orders_sent.load(); }
    uint64_t orders_acked() const { return m_orders_acked.load(); }
    
private:
    std::atomic<uint64_t> m_orders_sent{0};
    std::atomic<uint64_t> m_orders_acked{0};
    ReportQueue m_reports;
    
    // Simulated exchange connection
    void simulate_exchange_response(const Order* order);
};

}