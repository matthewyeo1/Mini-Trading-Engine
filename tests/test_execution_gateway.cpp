#include <gtest/gtest.h>
#include "velox/gateway/execution_gateway.hpp"

using namespace velox;

class ExecutionGatewayTest : public ::testing::Test {
protected:
    void SetUp() override {
        pool = std::make_unique<ExecutionGateway::ReportPool>();
        gateway = std::make_unique<ExecutionGateway>(pool.get());
        // Add 2 workers (simulating 2 CPU cores)
        gateway->add_worker();
        gateway->add_worker();
    }
    
    std::unique_ptr<ExecutionGateway::ReportPool> pool;
    std::unique_ptr<ExecutionGateway> gateway;
};

TEST_F(ExecutionGatewayTest, AddWorker) {
    EXPECT_EQ(gateway->worker_count(), 2);
    gateway->add_worker();
    EXPECT_EQ(gateway->worker_count(), 3);
}

TEST_F(ExecutionGatewayTest, SendReport) {
    ExecutionReport report;
    report.order_id = 12345;
    report.client_order_id = 67890;
    report.side = OrderSide::BUY;
    report.executed_quantity = 100;
    report.executed_price = 10000;
    report.status = OrderStatus::FILLED;
    std::strncpy(report.symbol, "AAPL", 7);
    
    bool sent = gateway->send_report(report);
    EXPECT_TRUE(sent);
    EXPECT_EQ(gateway->total_reports_sent(), 1);
}

TEST_F(ExecutionGatewayTest, ReceiveReport) {
    ExecutionReport report;
    report.order_id = 12345;
    std::strncpy(report.symbol, "AAPL", 7);
    
    gateway->send_report(report);
    
    auto* received = gateway->receive_report(0);
    ASSERT_NE(received, nullptr);
    EXPECT_EQ(received->order_id, 12345);
    EXPECT_STREQ(received->symbol, "AAPL");
}

TEST_F(ExecutionGatewayTest, RoundRobinWorkerSelection) {
    for (int i = 0; i < 4; ++i) {
        ExecutionReport report;
        report.order_id = i;
        gateway->send_report(report);
    }
    
    // Reports should be distributed across workers
    // Worker 0 gets reports 0 and 2, Worker 1 gets 1 and 3
    auto* r0 = gateway->receive_report(0);
    auto* r1 = gateway->receive_report(1);
    EXPECT_NE(r0, nullptr);
    EXPECT_NE(r1, nullptr);
}

TEST_F(ExecutionGatewayTest, SendOrder) {
    Order order;
    order.order_id = 1;
    order.client_order_id = 100;
    order.side = OrderSide::BUY;
    order.filled_quantity = 50;
    order.price = 10000;
    order.status = OrderStatus::PARTIAL;
    order.matched_timestamp = 123456789;
    std::strncpy(order.symbol, "AAPL", 7);
    
    bool sent = gateway->send_order(&order);
    EXPECT_TRUE(sent);
    EXPECT_EQ(gateway->total_orders_sent(), 1);
    EXPECT_EQ(gateway->total_reports_sent(), 1);
}

TEST_F(ExecutionGatewayTest, SendOrderNull) {
    EXPECT_FALSE(gateway->send_order(nullptr));
}

TEST_F(ExecutionGatewayTest, InvalidWorkerId) {
    ExecutionReport report;
    // Try worker 99 (doesn't exist)
    EXPECT_FALSE(gateway->send_report(report, 99));
}

TEST_F(ExecutionGatewayTest, ReceiveFromInvalidWorker) {
    EXPECT_EQ(gateway->receive_report(99), nullptr);
}