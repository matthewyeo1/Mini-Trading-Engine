#include "velox/gateway/execution_gateway.hpp"
#include <cstring>

namespace velox {

ExecutionGateway::ExecutionGateway() = default;
ExecutionGateway::~ExecutionGateway() = default;

void ExecutionGateway::add_worker() {
    m_worker_queues.push_back(std::make_unique<ReportQueue>());
}

bool ExecutionGateway::send_order(const Order* order, int worker_id) {
    if (!order) return false;

    // Convert order to execution report
    ExecutionReport report;
    report.order_id = order->order_id;
    report.client_order_id = order->client_order_id;
    report.side = order->side;
    report.executed_quantity = order->filled_quantity;
    report.executed_price = order->price;
    report.status = order->status;
    report.timestamp = order->matched_timestamp;
    std::strncpy(report.symbol, order->symbol, 7);

    m_total_orders.fetch_add(1, std::memory_order_relaxed);

    return send_report(report, worker_id);
}

bool ExecutionGateway::send_report(const ExecutionReport& report, int worker_id) {
    if (m_worker_queues.empty()) {
        return false;  
    }

    // Select worker
    if (worker_id < 0) {
        worker_id = m_next_worker.fetch_add(1, std::memory_order_relaxed) % m_worker_queues.size();
    }

    if (worker_id >= static_cast<int>(m_worker_queues.size())) {
        return false;
    }

    // Acquire report from pool
    auto report_ptr = m_report_pool.acquire();
    ExecutionReport* out = report_ptr.get();

    // Copy data
    out->order_id = report.order_id;
    out->client_order_id = report.client_order_id;
    out->side = report.side;
    out->status = report.status;
    out->executed_quantity = report.executed_quantity;
    out->executed_price = report.executed_price;
    out->timestamp = report.timestamp;
    out->reject_reason = report.reject_reason;
    std::strncpy(out->symbol, report.symbol, 7);
    out->symbol[7] = '\0';
    
    // Store for lifecycle management
    m_pending_reports.push_back(std::move(report_ptr));
    
    // Push to worker's queue
    bool success = m_worker_queues[worker_id]->push(out);
    if (success) {
        m_total_reports.fetch_add(1, std::memory_order_relaxed);
    }
    
    return success;
}

bool ExecutionGateway::cancel_order(uint64_t order_id) {
    (void)order_id;
    // TODO: implement cancel request to exchange
    return true;
}

ExecutionReport* ExecutionGateway::receive_report(int worker_id) {
    if (worker_id < 0 || worker_id >= static_cast<int>(m_worker_queues.size())) {
        return nullptr;
    }
    
    ExecutionReport* report = nullptr;
    auto opt_report = m_worker_queues[worker_id]->pop();

    if (opt_report.has_value()) {
        return opt_report.value();
    }
    return nullptr;
}

}