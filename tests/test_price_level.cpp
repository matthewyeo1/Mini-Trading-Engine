#include <gtest/gtest.h>
#include "velox/book/price_level.hpp"
#include "lockfree/pool.hpp"

using namespace velox;

class PriceLevelTest : public ::testing::Test {
protected:
    void SetUp() override {
        pool = std::make_unique<lockfree::ObjectPool<Order, 1000>>();
    }
    
    std::unique_ptr<lockfree::ObjectPool<Order, 1000>> pool;
};

TEST_F(PriceLevelTest, AddOrder) {
    PriceLevel level(10000);
    
    auto order = pool->acquire();
    order->order_id = 1;
    order->price = 10000;
    order->quantity = 100;
    order->remaining_quantity = 100;
    
    level.add_order(order.get());
    
    EXPECT_EQ(level.price(), 10000);
    EXPECT_EQ(level.total_quantity(), 100);
    EXPECT_FALSE(level.empty());
    EXPECT_EQ(level.head(), order.get());
    EXPECT_EQ(level.tail(), order.get());
}

TEST_F(PriceLevelTest, AddMultipleOrders) {
    PriceLevel level(10000);
    
    auto order1 = pool->acquire();
    order1->order_id = 1;
    order1->remaining_quantity = 50;
    
    auto order2 = pool->acquire();
    order2->order_id = 2;
    order2->remaining_quantity = 30;
    
    level.add_order(order1.get());
    level.add_order(order2.get());
    
    // LIFO order - second should be at head
    EXPECT_EQ(level.total_quantity(), 80);
    EXPECT_EQ(level.head(), order2.get());
    EXPECT_EQ(level.tail(), order1.get());
    EXPECT_EQ(order2->next, order1.get());
    EXPECT_EQ(order1->prev, order2.get());
}

TEST_F(PriceLevelTest, RemoveOrder) {
    PriceLevel level(10000);
    
    auto order1 = pool->acquire();
    order1->order_id = 1;
    order1->remaining_quantity = 50;
    
    auto order2 = pool->acquire();
    order2->order_id = 2;
    order2->remaining_quantity = 30;
    
    level.add_order(order1.get());
    level.add_order(order2.get());
    
    level.remove_order(order2.get());
    
    EXPECT_EQ(level.total_quantity(), 50);
    EXPECT_EQ(level.head(), order1.get());
    EXPECT_EQ(level.tail(), order1.get());
    EXPECT_EQ(order1->prev, nullptr);
    EXPECT_EQ(order1->next, nullptr);
}

TEST_F(PriceLevelTest, MatchOrderFullFill) {
    PriceLevel level(10000);
    
    auto buy = pool->acquire();
    buy->order_id = 1;
    buy->side = OrderSide::BUY;
    buy->price = 10000;
    buy->quantity = 100;
    buy->remaining_quantity = 100;
    buy->filled_quantity = 0; 
    
    level.add_order(buy.get());
    
    auto sell = pool->acquire();
    sell->order_id = 2;
    sell->side = OrderSide::SELL;
    sell->price = 10000;
    sell->quantity = 60;
    sell->remaining_quantity = 60;
    sell->filled_quantity = 0; 
    
    auto remaining = level.match_order(sell.get());
    
    EXPECT_EQ(buy->remaining_quantity, 40);
    EXPECT_EQ(sell->remaining_quantity, 0);
    EXPECT_EQ(remaining, nullptr);
    EXPECT_EQ(level.total_quantity(), 40);
}

TEST_F(PriceLevelTest, MatchOrderPartialFill) {
    PriceLevel level(10000);
    
    auto buy1 = pool->acquire();
    buy1->order_id = 1;
    buy1->quantity = 50;
    buy1->remaining_quantity = 50;
    buy1->filled_quantity = 0; 
    
    auto buy2 = pool->acquire();
    buy2->order_id = 2;
    buy2->quantity = 30;
    buy2->remaining_quantity = 30;
    buy2->filled_quantity = 0; 
    
    level.add_order(buy1.get());
    level.add_order(buy2.get());
    
    auto sell = pool->acquire();
    sell->order_id = 3;
    sell->side = OrderSide::SELL;
    sell->quantity = 60;
    sell->remaining_quantity = 60;
    
    auto remaining = level.match_order(sell.get());
    
    // buy2 (30) should be fully filled, buy1 (50) should have 20 left
    EXPECT_EQ(buy2->remaining_quantity, 0);
    EXPECT_EQ(buy1->remaining_quantity, 20);
    EXPECT_EQ(sell->remaining_quantity, 0);
    EXPECT_EQ(remaining, nullptr);
    EXPECT_EQ(level.total_quantity(), 20);
    EXPECT_EQ(level.head(), buy1.get());
}

TEST_F(PriceLevelTest, MatchOrderNotEnoughLiquidity) {
    PriceLevel level(10000);
    
    auto buy = pool->acquire();
    buy->quantity = 50;
    buy->remaining_quantity = 50;
    buy->filled_quantity = 0; 
    
    level.add_order(buy.get());
    
    auto sell = pool->acquire();
    sell->side = OrderSide::SELL;
    sell->quantity = 100;
    sell->remaining_quantity = 100;
    sell->filled_quantity = 0; 
    
    auto remaining = level.match_order(sell.get());
    
    EXPECT_EQ(buy->remaining_quantity, 0);
    EXPECT_EQ(sell->remaining_quantity, 50);  // 50 left unmatched
    EXPECT_EQ(remaining, sell.get());  // Returns the remaining order
    EXPECT_EQ(level.total_quantity(), 0);
    EXPECT_TRUE(level.empty());
}

TEST_F(PriceLevelTest, MatchOrderEmptyLevel) {
    PriceLevel level(10000);
    
    auto sell = pool->acquire();
    sell->side = OrderSide::SELL;
    sell->quantity = 100;
    sell->remaining_quantity = 100;
    sell->filled_quantity = 0; 
    
    auto remaining = level.match_order(sell.get());
    
    EXPECT_EQ(sell->remaining_quantity, 100);
    EXPECT_EQ(remaining, sell.get());
    EXPECT_EQ(level.total_quantity(), 0);
}
