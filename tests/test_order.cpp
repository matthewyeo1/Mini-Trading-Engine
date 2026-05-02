#include <gtest/gtest.h>
#include "velox/matching/order.hpp"
#include "velox/core/object_pool.hpp"
#include "velox/book/order_book.hpp"
#include <vector>

using namespace velox;
using namespace lockfree;

class PriceLevelTest : public ::testing::Test {
protected:
    void SetUp() override {
        pool = std::make_unique<lockfree::ObjectPool<Order, 1000>>();
    }

    std::unique_ptr<lockfree::ObjectPool<Order, 1000>> pool;
};

TEST(OrderTest, BasicOrderLifecycle) {
    ObjectPool<Order, 1000> pool;
    
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
    ObjectPool<Order, 1000> pool;
    
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
    ObjectPool<Order, 1000> pool;
    
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
    ObjectPool<Order, 10> pool;
    uintptr_t addr1 = 0;
    
    // Separate order1 & order2 into separate scopes
    {   
        auto order1 = pool.acquire();
        ASSERT_NE(order1.get(), nullptr);
        order1->order_id = 1;
        order1->price = 10000;
        addr1 = reinterpret_cast<uintptr_t>(order1.get());
        // PooledPtr of order1 will be released back to pool automatically
    }
    
    {
        auto order2 = pool.acquire();
        ASSERT_NE(order2.get(), nullptr);
        
        uintptr_t addr2 = reinterpret_cast<uintptr_t>(order2.get());
        
        // Should reuse the same memory
        EXPECT_EQ(addr1, addr2);
        
        order2->order_id = 2;
        EXPECT_EQ(order2->order_id, 2);
    }
}

TEST_F(PriceLevelTest, AddAndMatch) {
    PriceLevel level(10000);  // $100.00
    
    lockfree::ObjectPool<Order, 100> pool;
    std::vector<lockfree::PooledPtr<Order, 100>> owned;
    
    auto create_order = [&](uint64_t id, OrderSide side, int64_t price, uint32_t qty) -> Order* {
        auto order = pool.acquire();
        order->order_id = id;
        order->side = side;
        order->price = price;
        order->quantity = qty;
        order->remaining_quantity = qty;
        order->filled_quantity = 0;
        order->status = OrderStatus::NEW;
        Order* raw = order.get();
        owned.push_back(std::move(order));
        return raw;
    };
    
    auto buy1 = create_order(1, OrderSide::BUY, 10000, 50);
    auto buy2 = create_order(2, OrderSide::BUY, 10000, 30);
    
    level.add_order(buy1);
    level.add_order(buy2);
    
    EXPECT_EQ(level.total_quantity(), 80);
    
    auto sell = create_order(3, OrderSide::SELL, 10000, 60);
    
    std::vector<Fill> fills;                              
    auto result = level.match_order(sell, fills);        
    EXPECT_EQ(buy1->remaining_quantity, 0);
    EXPECT_EQ(buy2->remaining_quantity, 20);
    EXPECT_EQ(sell->remaining_quantity, 0);
    EXPECT_EQ(result, nullptr);                           
}