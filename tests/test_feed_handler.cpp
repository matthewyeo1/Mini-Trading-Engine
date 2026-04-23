#include <gtest/gtest.h>
#include "velox/feed/feed_handler.hpp"
#include "velox/book/order_book.hpp"
#include <fstream>
#include <cstring>

using namespace velox;

class FeedHandlerTest : public ::testing::Test {
protected:
    void SetUp() override {
        handler = std::make_unique<FeedHandler>();
        add_order_called = false;
        cancel_order_called = false;
        modify_order_called = false;
        trade_called = false;
    }
    
    // Create a mock ITCH Add Order message
    std::vector<uint8_t> create_add_order_msg(uint64_t order_id, 
                                               const char* symbol,
                                               char side,
                                               int64_t price,
                                               uint32_t quantity) {
        std::vector<uint8_t> msg(35, 0);
        
        // Length (2 bytes, big-endian)
        msg[0] = 0x00;
        msg[1] = 0x23;  // 35 bytes
        
        // Message type
        msg[2] = 'A';
        
        // Timestamp (8 bytes) - 13:00:00.000000000
        msg[3] = 0x00;
        msg[4] = 0x00;
        msg[5] = 0x00;
        msg[6] = 0x00;
        msg[7] = 0x0B;
        msg[8] = 0x8B;
        msg[9] = 0x8B;
        msg[10] = 0x00;
        
        // Tracking number (order ID, 8 bytes, big-endian)
        for (int i = 0; i < 8; ++i) {
            msg[11 + i] = (order_id >> (56 - i * 8)) & 0xFF;
        }
        
        // Stock symbol (6 bytes)
        for (int i = 0; i < 6 && symbol[i]; ++i) {
            msg[19 + i] = symbol[i];
        }
        
        // Side
        msg[25] = side;
        
        // Price (4 bytes, big-endian) - price * 10000
        uint32_t price_raw = static_cast<uint32_t>(price);
        msg[26] = (price_raw >> 24) & 0xFF;
        msg[27] = (price_raw >> 16) & 0xFF;
        msg[28] = (price_raw >> 8) & 0xFF;
        msg[29] = price_raw & 0xFF;
        
        // Quantity (4 bytes, big-endian)
        msg[30] = (quantity >> 24) & 0xFF;
        msg[31] = (quantity >> 16) & 0xFF;
        msg[32] = (quantity >> 8) & 0xFF;
        msg[33] = quantity & 0xFF;
        
        return msg;
    }
    
    // Create a mock ITCH Cancel Order message
    std::vector<uint8_t> create_cancel_msg(uint64_t order_id) {
        std::vector<uint8_t> msg(23, 0);
        
        msg[0] = 0x00;
        msg[1] = 0x17;  // 23 bytes
        msg[2] = 'X';
        
        // Timestamp
        msg[3] = 0x00;
        msg[4] = 0x00;
        msg[5] = 0x00;
        msg[6] = 0x00;
        msg[7] = 0x0B;
        msg[8] = 0x8B;
        msg[9] = 0x8B;
        msg[10] = 0x00;
        
        // Order ID
        for (int i = 0; i < 8; ++i) {
            msg[11 + i] = (order_id >> (56 - i * 8)) & 0xFF;
        }
        
        // Cancelled quantity (4 bytes)
        msg[19] = 0x00;
        msg[20] = 0x00;
        msg[21] = 0x00;
        msg[22] = 0x64;  // 100 shares
        
        return msg;
    }
    
    std::unique_ptr<FeedHandler> handler;
    bool add_order_called;
    bool cancel_order_called;
    bool modify_order_called;
    bool trade_called;
    Order last_order;
    uint64_t last_cancel_id;
};

TEST_F(FeedHandlerTest, ParseAddOrder) {
    auto msg = create_add_order_msg(0x12345678, "AAPL", 'B', 1000000, 100);
    
    handler->on_add_order([this](const Order& o) {
        add_order_called = true;
        last_order = o;
    });
    
    handler->process(reinterpret_cast<const char*>(msg.data()), msg.size());
    
    EXPECT_TRUE(add_order_called);
    EXPECT_EQ(last_order.order_id, 0x12345678);
    EXPECT_STREQ(last_order.symbol, "AAPL");
    EXPECT_EQ(last_order.side, OrderSide::BUY);
    EXPECT_EQ(last_order.price, 1000000);
    EXPECT_EQ(last_order.quantity, 100);
    EXPECT_EQ(last_order.remaining_quantity, 100);
    EXPECT_EQ(last_order.type, OrderType::LIMIT);
    EXPECT_EQ(last_order.status, OrderStatus::NEW);
}

TEST_F(FeedHandlerTest, ParseCancelOrder) {
    auto msg = create_cancel_msg(0x12345678);
    
    handler->on_cancel_order([this](uint64_t id) {
        cancel_order_called = true;
        last_cancel_id = id;
    });
    
    handler->process(reinterpret_cast<const char*>(msg.data()), msg.size());
    
    EXPECT_TRUE(cancel_order_called);
    EXPECT_EQ(last_cancel_id, 0x12345678);
}

TEST_F(FeedHandlerTest, ParseMultipleMessages) {
    auto add_msg = create_add_order_msg(0x1111, "AAPL", 'B', 1000000, 100);
    auto cancel_msg = create_cancel_msg(0x1111);
    
    // Collect both messages into a single buffer
    std::vector<uint8_t> buffer;
    buffer.insert(buffer.end(), add_msg.begin(), add_msg.end());
    buffer.insert(buffer.end(), cancel_msg.begin(), cancel_msg.end());
    
    handler->on_add_order([this](const Order&) { add_order_called = true; });
    handler->on_cancel_order([this](uint64_t) { cancel_order_called = true; });
    
    handler->process(reinterpret_cast<const char*>(buffer.data()), buffer.size());
    
    EXPECT_TRUE(add_order_called);
    EXPECT_TRUE(cancel_order_called);
    EXPECT_EQ(handler->message_count(), 2);
}

TEST_F(FeedHandlerTest, ParseSellOrder) {
    auto msg = create_add_order_msg(0x12345678, "MSFT", 'S', 1000000, 50);
    
    handler->on_add_order([this](const Order& o) {
        add_order_called = true;
        last_order = o;
    });
    
    handler->process(reinterpret_cast<const char*>(msg.data()), msg.size());
    
    EXPECT_TRUE(add_order_called);
    EXPECT_EQ(last_order.side, OrderSide::SELL);
}

TEST_F(FeedHandlerTest, SymbolTrimming) {
    auto msg = create_add_order_msg(0x12345678, "AAPL  ", 'B', 1000000, 100);
    
    handler->on_add_order([this](const Order& o) {
        add_order_called = true;
        last_order = o;
    });
    
    handler->process(reinterpret_cast<const char*>(msg.data()), msg.size());
    
    EXPECT_STREQ(last_order.symbol, "AAPL");
}

TEST_F(FeedHandlerTest, MessageCount) {
    auto msg1 = create_add_order_msg(0x1111, "AAPL", 'B', 1000000, 100);
    auto msg2 = create_add_order_msg(0x2222, "MSFT", 'B', 1000000, 100);
    
    std::vector<uint8_t> buffer;
    buffer.insert(buffer.end(), msg1.begin(), msg1.end());
    buffer.insert(buffer.end(), msg2.begin(), msg2.end());
    
    handler->on_add_order([](const Order&) {});
    handler->process(reinterpret_cast<const char*>(buffer.data()), buffer.size());
    
    EXPECT_EQ(handler->message_count(), 2);
    EXPECT_EQ(handler->error_count(), 0);
}

TEST_F(FeedHandlerTest, UnknownMessageType) {
    // Create message with unknown type 'Z'
    std::vector<uint8_t> msg(20, 0);
    msg[0] = 0x00;
    msg[1] = 0x14;  // 20 bytes
    msg[2] = 'Z';   // Unknown type
    
    handler->process(reinterpret_cast<const char*>(msg.data()), msg.size());
    
    // Unknown messages should be skipped, not counted as errors
    EXPECT_EQ(handler->message_count(), 0);
    EXPECT_EQ(handler->error_count(), 0);
}

TEST_F(FeedHandlerTest, InvalidMessageLength) {
    std::vector<uint8_t> msg(5, 0);
    msg[0] = 0x00;
    msg[1] = 0xFF;  // Claims 255 bytes but only 5 available
    msg[2] = 'A';
    
    handler->process(reinterpret_cast<const char*>(msg.data()), msg.size());
    
    EXPECT_EQ(handler->error_count(), 1);
}

TEST_F(FeedHandlerTest, EmptyBuffer) {
    handler->process(nullptr, 0);
    EXPECT_EQ(handler->message_count(), 0);
    EXPECT_EQ(handler->error_count(), 0);
}

TEST_F(FeedHandlerTest, MultipleCallbacks) {
    auto msg = create_add_order_msg(0x12345678, "AAPL", 'B', 1000000, 100);
    
    int callback_count = 0;
    handler->on_add_order([&callback_count](const Order&) { callback_count++; });
    handler->on_add_order([&callback_count](const Order&) { callback_count++; });
    
    handler->process(reinterpret_cast<const char*>(msg.data()), msg.size());
    
    EXPECT_EQ(callback_count, 2);
}

TEST_F(FeedHandlerTest, ProcessRealFile) {
    // This test will be skipped if the file doesn't exist
    const std::string filename = "test_data/NASDAQ_ITCH50_sample.bin";
    
    std::ifstream test_file(filename, std::ios::binary);
    if (!test_file.good()) {
        GTEST_SKIP() << "Test file not found: " << filename;
    }
    test_file.close();
    
    handler->on_add_order([](const Order& o) {
        (void)o;
        // Just verify we can parse without crashing
    });
    
    handler->process_file(filename);
    
    // At least some messages should have been processed
    EXPECT_GT(handler->message_count(), 0);
}

TEST_F(FeedHandlerTest, IntegrationWithOrderBook) {
    // Create a simple order book
    OrderBook book("AAPL");
    
    // Connect feed to order book
    handler->on_add_order([&book](const Order& o) {
        // Create a mutable copy (in production, would use pool)
        Order order = o;
        book.add_order(&order);
    });
    
    // Create and process an add order
    auto msg = create_add_order_msg(0x12345678, "AAPL", 'B', 1000000, 100);
    handler->process(reinterpret_cast<const char*>(msg.data()), msg.size());
    
    // Verify order was added to book
    EXPECT_EQ(book.best_bid(), 1000000);
    EXPECT_EQ(book.bid_depth(), 100);
    EXPECT_EQ(book.bid_levels(), 1);
}