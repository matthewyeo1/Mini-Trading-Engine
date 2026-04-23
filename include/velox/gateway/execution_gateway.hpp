#pragma once
#include <atomic>
#include <cstdint>
#include <vector>
#include "velox/matching/order.hpp"
#include "lockfree/spsc_queue.hpp"
#include "lockfree/pool.hpp"

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
    using ReportPool = lockfree::ObjectPool<ExecutionReport, 65536>;
    using ReportQueue = lockfree::SPSCQueue<ExecutionReport*, 4096>;
    
    // Default constructor (creates its own pool)
    ExecutionGateway();
    
    // Constructor with external pool
    explicit ExecutionGateway(ReportPool* pool);
    
    ~ExecutionGateway();
    
    // Send order to exchange (simulated)
    bool send_order(const Order* order, int worker_id = -1);

    // Send report from matching engine
    bool send_report(const ExecutionReport& report, int worker_id = -1);
    
    // Cancel order
    bool cancel_order(uint64_t order_id);
    
    // Receive execution reports (consumer thread)
    ExecutionReport* receive_report(int worker_id);

    // Add worker queue
    void add_worker();
    
    // Stats
    uint64_t total_reports_sent() const { return m_total_reports.load(); }
    uint64_t total_orders_sent() const { return m_total_orders.load(); }
    size_t worker_count() const { return m_worker_queues.size(); }
    
private:
    // 1:1 queue to worker ratio
    std::vector<std::unique_ptr<ReportQueue>> m_worker_queues;

    // Reusable memory pool for reports
    ReportPool* m_report_pool;
    bool m_owns_pool = false;

    // Stats
    std::atomic<uint64_t> m_total_reports{0};
    std::atomic<uint64_t> m_total_orders{0};
    
    // Round-robin counter for worker selection
    std::atomic<int> m_next_worker{0};
    
    // Simulated exchange connection
    void simulate_exchange_response(const ExecutionReport& report);
};

}