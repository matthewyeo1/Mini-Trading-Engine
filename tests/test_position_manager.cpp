#include <gtest/gtest.h>
#include "velox/risk/position_manager.hpp"
#include "lockfree/pool.hpp"
#include <cstring>

using namespace velox;

class PositionManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        pool = std::make_unique<lockfree::ObjectPool<Order, 1000>>();
        mgr = std::make_unique<PositionManager>();
    }
    
    Order* create_order(uint64_t id, OrderSide side, const char* sym, int64_t price, uint32_t qty) {
        auto o = pool->acquire();
        o->order_id = id;
        o->side = side;
        o->price = price;
        o->quantity = qty;
        o->remaining_quantity = qty;
        std::strncpy(o->symbol, sym, 7);
        Order* raw = o.get();
        owned.push_back(std::move(o));
        return raw;
    }
    
    std::unique_ptr<lockfree::ObjectPool<Order, 1000>> pool;
    std::unique_ptr<PositionManager> mgr;
    std::vector<lockfree::PooledPtr<Order, 1000>> owned;
};

TEST_F(PositionManagerTest, BuyCreatesLongPosition) {
    auto order = create_order(1, OrderSide::BUY, "AAPL", 10000, 100);
    mgr->update_position(order, 100, 10000);
    
    EXPECT_EQ(mgr->get_position("AAPL"), 100);
    EXPECT_EQ(mgr->get_realized_pnl("AAPL"), 0);
    EXPECT_DOUBLE_EQ(mgr->get_unrealized_pnl("AAPL", 10100), 100 * 100); // 100 * 100 = 10000
}

TEST_F(PositionManagerTest, SellClosesPosition) {
    auto buy = create_order(1, OrderSide::BUY, "AAPL", 10000, 100);
    mgr->update_position(buy, 100, 10000);
    
    auto sell = create_order(2, OrderSide::SELL, "AAPL", 10100, 100);
    mgr->update_position(sell, 100, 10100);
    
    EXPECT_EQ(mgr->get_position("AAPL"), 0);
    EXPECT_EQ(mgr->get_realized_pnl("AAPL"), 100 * 100); // profit = (10100-10000)*100 = 10000
    EXPECT_EQ(mgr->get_unrealized_pnl("AAPL", 10100), 0);
}

TEST_F(PositionManagerTest, PartialFillLeavesRemaining) {
    auto buy = create_order(1, OrderSide::BUY, "AAPL", 10000, 100);
    mgr->update_position(buy, 60, 10000);
    
    EXPECT_EQ(mgr->get_position("AAPL"), 60);
    EXPECT_EQ(mgr->get_unrealized_pnl("AAPL", 10100), 60 * 100);
}

TEST_F(PositionManagerTest, MultipleBuysWeightedAverage) {
    mgr->update_position(create_order(1, OrderSide::BUY, "AAPL", 10000, 100), 100, 10000);
    mgr->update_position(create_order(2, OrderSide::BUY, "AAPL", 10200, 100), 100, 10200);
    
    EXPECT_EQ(mgr->get_position("AAPL"), 200);
    // Avg price = (10000*100 + 10200*100)/200 = 10100
    EXPECT_EQ(mgr->get_unrealized_pnl("AAPL", 10100), 0);
    EXPECT_EQ(mgr->get_unrealized_pnl("AAPL", 10200), 20000); // 200 * (10200-10100) = 20000
}

TEST_F(PositionManagerTest, DifferentSymbolsIndependent) {
    mgr->update_position(create_order(1, OrderSide::BUY, "AAPL", 10000, 100), 100, 10000);
    mgr->update_position(create_order(2, OrderSide::BUY, "MSFT", 20000, 50), 50, 20000);
    
    EXPECT_EQ(mgr->get_position("AAPL"), 100);
    EXPECT_EQ(mgr->get_position("MSFT"), 50);
    EXPECT_EQ(mgr->get_unrealized_pnl("AAPL", 10100), 10000);
    EXPECT_EQ(mgr->get_unrealized_pnl("MSFT", 20100), 50 * 100);
}

TEST_F(PositionManagerTest, ResetClearsAll) {
    mgr->update_position(create_order(1, OrderSide::BUY, "AAPL", 10000, 100), 100, 10000);
    mgr->reset();
    EXPECT_EQ(mgr->get_position("AAPL"), 0);
    EXPECT_EQ(mgr->get_realized_pnl("AAPL"), 0);
}