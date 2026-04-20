// tests/test_order_book.cpp
#include <gtest/gtest.h>
#include "velox/book/order_book.hpp"
#include "lockfree/pool.hpp"

using namespace velox;

class OrderBookTest : public ::testing::Test {
protected:
    void SetUp() override {
        pool = std::make_unique<lockfree::ObjectPool<Order, 10000>>();
        book = std::make_unique<OrderBook>("AAPL");
        owned_orders.clear();
    }
    
    Order* create_order(uint64_t id, OrderSide side, int64_t price, uint32_t qty) {
        auto order = pool->acquire();
        order->order_id = id;
        order->side = side;
        order->price = price;
        order->quantity = qty;
        order->remaining_quantity = qty;
        order->filled_quantity = 0;
        Order* ptr = order.get();
        owned_orders.push_back(std::move(order));
        return ptr;
    }
    
    std::unique_ptr<lockfree::ObjectPool<Order, 10000>> pool;
    std::unique_ptr<OrderBook> book;
    std::vector<lockfree::PooledPtr<Order, 10000>> owned_orders;
};

TEST_F(OrderBookTest, AddBidOrder) {
    auto order = create_order(1, OrderSide::BUY, 10000, 100);
    
    EXPECT_TRUE(book->add_order(order));
    EXPECT_EQ(book->best_bid(), 10000);
    EXPECT_EQ(book->bid_depth(), 100);
    EXPECT_EQ(book->bid_levels(), 1);
}

TEST_F(OrderBookTest, AddAskOrder) {
    auto order = create_order(1, OrderSide::SELL, 10100, 50);
    
    EXPECT_TRUE(book->add_order(order));
    EXPECT_EQ(book->best_ask(), 10100);
    EXPECT_EQ(book->ask_depth(), 50);
    EXPECT_EQ(book->ask_levels(), 1);
}

TEST_F(OrderBookTest, MatchBuyWithExistingSell) {
    // Add sell order at $100
    auto sell = create_order(1, OrderSide::SELL, 10000, 100);
    book->add_order(sell);
    EXPECT_EQ(book->best_ask(), 10000);
    
    // Incoming buy order at $101 (crosses)
    auto buy = create_order(2, OrderSide::BUY, 10100, 60);
    auto remaining = book->match(buy);
    
    // Buy should be fully filled
    EXPECT_EQ(buy->remaining_quantity, 0);
    EXPECT_EQ(sell->remaining_quantity, 40);
    EXPECT_EQ(remaining, nullptr);
    EXPECT_EQ(book->best_ask(), 10000);
    EXPECT_EQ(book->ask_depth(), 40);
}

TEST_F(OrderBookTest, MatchSellWithExistingBuy) {
    // Add buy order at $100
    auto buy = create_order(1, OrderSide::BUY, 10000, 100);
    book->add_order(buy);
    EXPECT_EQ(book->best_bid(), 10000);
    
    // Incoming sell order at $99 (crosses)
    auto sell = create_order(2, OrderSide::SELL, 9900, 60);
    auto remaining = book->match(sell);
    
    // Sell should be fully filled
    EXPECT_EQ(sell->remaining_quantity, 0);
    EXPECT_EQ(buy->remaining_quantity, 40);
    EXPECT_EQ(remaining, nullptr);
    EXPECT_EQ(book->best_bid(), 10000);
    EXPECT_EQ(book->bid_depth(), 40);
}

TEST_F(OrderBookTest, PartialFillRemainingGoesToBook) {
    // Add sell order at $100
    auto sell = create_order(1, OrderSide::SELL, 10000, 50);
    book->add_order(sell);
    
    // Incoming buy order for 100 (only 50 available)
    auto buy = create_order(2, OrderSide::BUY, 10100, 100);
    auto remaining = book->match(buy);
    
    // Buy should have 50 left and be added to book
    EXPECT_EQ(buy->remaining_quantity, 50);
    EXPECT_EQ(sell->remaining_quantity, 0);
    EXPECT_EQ(remaining, buy);
    EXPECT_EQ(book->best_bid(), 10100);
    EXPECT_EQ(book->bid_depth(), 50);
    EXPECT_EQ(book->ask_levels(), 0);  // Sell level removed
}

TEST_F(OrderBookTest, MultiplePriceLevels) {
    // Add buy orders at different prices
    auto buy1 = create_order(1, OrderSide::BUY, 10000, 50);
    auto buy2 = create_order(2, OrderSide::BUY, 9900, 30);
    auto buy3 = create_order(3, OrderSide::BUY, 10100, 20);
    
    book->add_order(buy1);
    book->add_order(buy2);
    book->add_order(buy3);
    
    // Best bid should be highest price
    EXPECT_EQ(book->best_bid(), 10100);
    EXPECT_EQ(book->bid_levels(), 3);
    
    // Match a sell order at 10050 (should fill 10100 level only)
    auto sell = create_order(4, OrderSide::SELL, 10050, 15);
    auto remaining = book->match(sell);
    
    EXPECT_EQ(buy3->remaining_quantity, 5);  // 20 - 15 = 5 left
    EXPECT_EQ(sell->remaining_quantity, 0);
    EXPECT_EQ(remaining, nullptr);
    EXPECT_EQ(book->best_bid(), 10100);
    EXPECT_EQ(book->bid_depth(), 85);  // 5 + 50 + 30 = 85
}

TEST_F(OrderBookTest, EmptyBook) {
    EXPECT_EQ(book->best_bid(), 0);
    EXPECT_EQ(book->best_ask(), INT64_MAX);
    EXPECT_EQ(book->bid_depth(), 0);
    EXPECT_EQ(book->ask_depth(), 0);
}
