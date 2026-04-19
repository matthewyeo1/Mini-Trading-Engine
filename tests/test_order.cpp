#include <gtest/gtest.h>
#include "velox/matching/order.hpp"
#include "velox/core/object_pool.hpp"

using namespace velox;

TEST(OrderTest, BasicOrderLifecycle) {
    lockfree::ObjectPool<Order, 1000> pool;
    
    auto order = pool.acquire();
    ASSERT_NE(order.get(), nullptr);
    
    order->order_id = 12345;
    order->client_order_id = 67890;
    order->side = OrderSide::BUY;
    order->type = OrderType::LIMIT;
    order->price = 10000;
    order->quantity = 100;
    order->remaining_quantity = 100;
    
    EXPECT_EQ(order->order_id, 12345);
    EXPECT_EQ(order->client_order_id, 67890);
    EXPECT_TRUE(order->is_buy());
    EXPECT_TRUE(order->is_limit());
    EXPECT_FALSE(order->is_filled());
    EXPECT_TRUE(order->is_active());
    
    order->fill(60);
    EXPECT_EQ(order->filled_quantity, 60);
    EXPECT_EQ(order->remaining_quantity, 40);
    EXPECT_EQ(order->status, OrderStatus::PARTIAL);
    
    order->fill(40);
    EXPECT_TRUE(order->is_filled());
    EXPECT_EQ(order->status, OrderStatus::FILLED);
}

TEST(OrderTest, CancelOrder) {
    lockfree::ObjectPool<Order, 1000> pool;
    
    auto order = pool.acquire();
    ASSERT_NE(order.get(), nullptr);
    
    order->order_id = 1;
    order->quantity = 100;
    order->remaining_quantity = 100;
    
    EXPECT_TRUE(order->is_active());
    
    order->cancel();
    EXPECT_EQ(order->status, OrderStatus::CANCELLED);
    EXPECT_FALSE(order->is_active());
}

TEST(OrderTest, ResetOrder) {
    lockfree::ObjectPool<Order, 1000> pool;
    
    auto order = pool.acquire();
    ASSERT_NE(order.get(), nullptr);
    
    order->order_id = 123;
    order->side = OrderSide::BUY;
    order->price = 10000;
    order->quantity = 100;
    order->remaining_quantity = 100;
    order->fill(50);
    
    EXPECT_EQ(order->filled_quantity, 50);
    
    order->reset();
    
    EXPECT_EQ(order->order_id, 0);
    EXPECT_EQ(order->client_order_id, 0);
    EXPECT_EQ(order->price, 0);
    EXPECT_EQ(order->quantity, 0);
    EXPECT_EQ(order->filled_quantity, 0);
    EXPECT_EQ(order->remaining_quantity, 0);
    EXPECT_EQ(order->status, OrderStatus::NEW);
    EXPECT_EQ(order->prev, nullptr);
    EXPECT_EQ(order->next, nullptr);
}

TEST(OrderTest, ObjectPoolReuse) {
    lockfree::ObjectPool<Order, 10> pool;
    
    auto order1 = pool.acquire();
    ASSERT_NE(order1.get(), nullptr);
    order1->order_id = 1;
    order1->price = 10000;
    
    uintptr_t addr1 = reinterpret_cast<uintptr_t>(order1.get());
    
    // Release back to pool
    order1.release();
    
    auto order2 = pool.acquire();
    ASSERT_NE(order2.get(), nullptr);
    
    uintptr_t addr2 = reinterpret_cast<uintptr_t>(order2.get());
    
    // Should reuse the same memory
    EXPECT_EQ(addr1, addr2);
    
    order2->order_id = 2;
    EXPECT_EQ(order2->order_id, 2);
}

/*
TEST(PriceLevelTest, AddAndMatch) {
    PriceLevel level(10000);  // $100.00
    
    OrderPool pool;
    auto buy1 = pool.acquire();
    buy1->side = OrderSide::BUY;
    buy1->price = 10000;
    buy1->quantity = 50;
    buy1->remaining_quantity = 50;
    
    auto buy2 = pool.acquire();
    buy2->side = OrderSide::BUY;
    buy2->price = 10000;
    buy2->quantity = 30;
    buy2->remaining_quantity = 30;
    
    level.add_order(buy1.get());
    level.add_order(buy2.get());
    
    EXPECT_EQ(level.total_quantity(), 80);
    
    // Incoming sell order
    auto sell = pool.acquire();
    sell->side = OrderSide::SELL;
    sell->price = 10000;
    sell->quantity = 60;
    sell->remaining_quantity = 60;
    
    auto result = level.match_order(sell.get());
    
    // Should partially fill
    EXPECT_EQ(buy1->remaining_quantity, 0);
    EXPECT_EQ(buy2->remaining_quantity, 20);
    EXPECT_EQ(sell->remaining_quantity, 0);
}
*/