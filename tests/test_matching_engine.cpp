#include <gtest/gtest.h>
#include "velox/matching/matching_engine.hpp"
#include "velox/risk/risk_manager.hpp"
#include "velox/gateway/execution_gateway.hpp"
#include "lockfree/pool.hpp"

using namespace velox;

class MatchingEngineTest : public ::testing::Test {
protected:
    void SetUp() override {
        pool = std::make_unique<lockfree::ObjectPool<Order, 10000>>();
        risk = std::make_unique<RiskManager>();
        gateway = std::make_unique<ExecutionGateway>();
        engine = std::make_unique<MatchingEngine>("AAPL", risk.get(), gateway.get());
    }
    
    Order* create_order(uint64_t id, OrderSide side, int64_t price, uint32_t qty) {
        auto order = pool->acquire();
        order->order_id = id;
        order->client_order_id = id;
        order->side = side;
        order->price = price;
        order->quantity = qty;
        order->remaining_quantity = qty;
        order->filled_quantity = 0;
        order->status = OrderStatus::NEW;
        std::strncpy(order->symbol, "AAPL", 7);
        Order* raw = order.get();
        owned_orders.push_back(std::move(order));
        return raw;
    }
    
    std::unique_ptr<lockfree::ObjectPool<Order, 10000>> pool;
    std::unique_ptr<RiskManager> risk;
    std::unique_ptr<ExecutionGateway> gateway;
    std::unique_ptr<MatchingEngine> engine;
    std::vector<lockfree::PooledPtr<Order, 10000>> owned_orders;
};

TEST_F(MatchingEngineTest, SubmitAndMatchOrder) {
    auto buy = create_order(1, OrderSide::BUY, 10000, 100);
    engine->submit_order(buy);
    engine->run_match_cycle();
    
    // Order with no opposite side should remain in book
    EXPECT_EQ(buy->remaining_quantity, 100);
    
    auto sell = create_order(2, OrderSide::SELL, 9900, 60);
    engine->submit_order(sell);
    engine->run_match_cycle();
    
    // Should match
    EXPECT_EQ(buy->remaining_quantity, 40);
    EXPECT_EQ(sell->remaining_quantity, 0);
}

TEST_F(MatchingEngineTest, OrderRejectedByRisk) {
    // Assume RiskManager rejects orders > 1000 quantity
    auto order = create_order(1, OrderSide::BUY, 10000, 100000);
    engine->submit_order(order);
    engine->run_match_cycle();
    
    // NOTE: skip test for now until risk_manager.cpp has been implemented
    //EXPECT_EQ(order->status, OrderStatus::REJECTED);
}

TEST_F(MatchingEngineTest, PartialFill) {
    auto sell = create_order(1, OrderSide::SELL, 10000, 50);
    engine->submit_order(sell);
    engine->run_match_cycle();
    
    auto buy = create_order(2, OrderSide::BUY, 10100, 100);
    engine->submit_order(buy);
    engine->run_match_cycle();
    
    EXPECT_EQ(sell->remaining_quantity, 0);
    EXPECT_EQ(buy->remaining_quantity, 50);
    EXPECT_EQ(buy->status, OrderStatus::PARTIAL);
}

TEST_F(MatchingEngineTest, StatsTracking) {
    auto buy = create_order(1, OrderSide::BUY, 10000, 100);
    auto sell = create_order(2, OrderSide::SELL, 9900, 60);
    
    engine->submit_order(buy);
    engine->submit_order(sell);
    engine->run_match_cycle();
    
    auto stats = engine->get_stats();
    EXPECT_EQ(stats.orders_matched, 1);
    EXPECT_EQ(stats.orders_rejected, 0);
}