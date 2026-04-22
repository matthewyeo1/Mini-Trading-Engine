#include "velox/gateway/execution_gateway.hpp"

namespace velox {

ExecutionGateway::ExecutionGateway() = default;
ExecutionGateway::~ExecutionGateway() = default;

bool ExecutionGateway::send_order(const Order* order) {
    (void)order;
    return true;
}

bool ExecutionGateway::send_report(const ExecutionReport& report) {
    (void)report;
    return true;
}

bool ExecutionGateway::cancel_order(uint64_t order_id) {
    (void)order_id;
    return true;
}

ExecutionReport* ExecutionGateway::receive_report() {
    return nullptr;
}

}