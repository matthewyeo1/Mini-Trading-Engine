#include <gtest/gtest.h>
#include "velox/book/price_level.hpp"
#include "lockfree/pool.hpp"
#include "velox/book/fill.hpp"

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
    order->remaining_quantity = 100;

    level.add_order(order.get());

    EXPECT_EQ(level.price(), 10000);
    EXPECT_EQ(level.total_quantity(), 100);
    EXPECT_FALSE(level.empty());
    EXPECT_EQ(level.head(), order.get());
    EXPECT_EQ(level.tail(), order.get());
}

TEST_F(PriceLevelTest, AddMultipleOrders_FIFO) {
    PriceLevel level(10000);

    auto o1 = pool->acquire();
    auto o2 = pool->acquire();

    o1->remaining_quantity = 50;
    o2->remaining_quantity = 30;

    level.add_order(o1.get());
    level.add_order(o2.get());

    EXPECT_EQ(level.total_quantity(), 80);
    EXPECT_EQ(level.head(), o1.get());
    EXPECT_EQ(level.tail(), o2.get());
}

TEST_F(PriceLevelTest, RemoveOrder_IsImplicit_FIFO) {
    std::vector<Fill> fills;
    fills.reserve(100);

    PriceLevel level(10000);

    auto o1 = pool->acquire();
    auto o2 = pool->acquire();

    o1->remaining_quantity = 50;
    o2->remaining_quantity = 30;

    level.add_order(o1.get());
    level.add_order(o2.get());

    // simulate full fill of o2 via match (not remove_order)
    o2->remaining_quantity = 0;

    level.match_order(o2.get(), fills); // safe no-op-ish

    EXPECT_EQ(level.head(), o1.get());
}

TEST_F(PriceLevelTest, MatchOrderFullFill) {
    std::vector<Fill> fills;
    fills.reserve(100);

    PriceLevel level(10000);

    auto buy = pool->acquire();
    buy->remaining_quantity = 100;

    level.add_order(buy.get());

    auto sell = pool->acquire();
    sell->remaining_quantity = 60;

    auto remaining = level.match_order(sell.get(), fills);

    EXPECT_EQ(buy->remaining_quantity, 40);
    EXPECT_EQ(sell->remaining_quantity, 0);
    EXPECT_EQ(remaining, nullptr);
    EXPECT_EQ(level.total_quantity(), 40);
}

TEST_F(PriceLevelTest, MatchOrderPartialFillAcrossQueue) {
    std::vector<Fill> fills;
    fills.reserve(100);

    PriceLevel level(10000);

    auto b1 = pool->acquire();
    auto b2 = pool->acquire();

    b1->remaining_quantity = 50;
    b2->remaining_quantity = 30;

    level.add_order(b1.get());
    level.add_order(b2.get());

    auto sell = pool->acquire();
    sell->remaining_quantity = 60;

    level.match_order(sell.get(), fills);

    EXPECT_EQ(b1->remaining_quantity, 0);
    EXPECT_EQ(b2->remaining_quantity, 20);
    EXPECT_EQ(level.total_quantity(), 20);
    EXPECT_EQ(level.head(), b2.get());
}

TEST_F(PriceLevelTest, MatchOrderEmptyLevel) {
    std::vector<Fill> fills;
    fills.reserve(100);

    PriceLevel level(10000);

    auto sell = pool->acquire();
    sell->remaining_quantity = 100;

    auto remaining = level.match_order(sell.get(), fills);

    EXPECT_EQ(remaining, sell.get());
    EXPECT_EQ(level.total_quantity(), 0);
}