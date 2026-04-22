#include <gtest/gtest.h>
#include "velox/risk/risk_manager.hpp"
#include "velox/matching/order.hpp"

using namespace velox;

class RiskManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        risk = std::make_unique<RiskManager>();
    }
    
    std::unique_ptr<RiskManager> risk;
};

TEST_F(RiskManagerTest, CheckOrder_ValidBuyOrder) {
    Order order;
    order.side = OrderSide::BUY;
    order.quantity = 100;
    order.price = 10000;
    std::strncpy(order.symbol, "AAPL", 7);
    
    EXPECT_TRUE(risk->check_order(&order));
}

TEST_F(RiskManagerTest, CheckOrder_ValidSellOrder) {
    Order order;
    order.side = OrderSide::SELL;
    order.quantity = 50;
    order.price = 10100;
    std::strncpy(order.symbol, "AAPL", 7);
    
    EXPECT_TRUE(risk->check_order(&order));
}

TEST_F(RiskManagerTest, CheckOrder_ZeroQuantity) {
    Order order;
    order.side = OrderSide::BUY;
    order.quantity = 0;
    order.price = 10000;
    std::strncpy(order.symbol, "AAPL", 7);
    
    EXPECT_FALSE(risk->check_order(&order));
}

TEST_F(RiskManagerTest, CheckOrder_NegativePrice) {
    Order order;
    order.side = OrderSide::BUY;
    order.quantity = 100;
    order.price = -1000;
    std::strncpy(order.symbol, "AAPL", 7);
    
    EXPECT_FALSE(risk->check_order(&order));
}

TEST_F(RiskManagerTest, CheckOrder_NullOrder) {
    EXPECT_FALSE(risk->check_order(nullptr));
}

TEST_F(RiskManagerTest, CircuitBreaker) {
    Order order;
    order.side = OrderSide::BUY;
    order.quantity = 100;
    order.price = 10000;
    std::strncpy(order.symbol, "AAPL", 7);
    
    EXPECT_TRUE(risk->check_order(&order));
    
    risk->activate_circuit_breaker();
    EXPECT_TRUE(risk->is_circuit_breaker_active());
    EXPECT_FALSE(risk->check_order(&order));
    
    risk->reset_circuit_breaker();
    EXPECT_FALSE(risk->is_circuit_breaker_active());
    EXPECT_TRUE(risk->check_order(&order));
}

TEST_F(RiskManagerTest, PositionLimit) {
    risk->set_position_limit("AAPL", 1000);
    
    Order buy_order;
    buy_order.side = OrderSide::BUY;
    buy_order.quantity = 500;
    buy_order.price = 10000;
    std::strncpy(buy_order.symbol, "AAPL", 7);
    
    // Should pass within limit
    EXPECT_TRUE(risk->check_position(&buy_order, 500));
    
    // Simulate position update (in real use, position manager would do this)
    // For now, we just test the check
    
    // Try to buy more than limit (1000 total)
    // This test is simplified - in reality, position would be tracked
}

TEST_F(RiskManagerTest, DifferentSymbols) {
    Order aapl;
    aapl.side = OrderSide::BUY;
    aapl.quantity = 1000;
    aapl.price = 10000;
    std::strncpy(aapl.symbol, "AAPL", 7);
    
    Order msft;
    msft.side = OrderSide::BUY;
    msft.quantity = 1000;
    msft.price = 10000;
    std::strncpy(msft.symbol, "MSFT", 7);
    
    risk->set_position_limit("AAPL", 500);
    risk->set_position_limit("MSFT", 2000);
    
    // AAPL order should be rejected (1000 > 500 limit)
    // MSFT order should be accepted (1000 < 2000 limit)
    // Note: check_position only checks against limit, doesn't track cumulative
    EXPECT_FALSE(risk->check_position(&aapl, 1000));
    EXPECT_TRUE(risk->check_position(&msft, 1000));
}

TEST_F(RiskManagerTest, HashSymbol) {
    // Test that hash is consistent
    uint32_t hash1 = risk->hash_symbol("AAPL");
    uint32_t hash2 = risk->hash_symbol("AAPL");
    EXPECT_EQ(hash1, hash2);
    
    // Different symbols should (likely) produce different hashes
    uint32_t hash3 = risk->hash_symbol("MSFT");
    EXPECT_NE(hash1, hash3);
}