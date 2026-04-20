#pragma once
#include <cstdint>
#include <cstring>

namespace velox {

enum class OrderSide : uint8_t {
    BUY = 0,
    SELL = 1
};

enum class OrderType : uint8_t {
    LIMIT = 0,
    MARKET = 1,
    IOC = 2,    // Immediate-or-Cancel
    FOK = 3     // Fill-or-Kill
};

enum class OrderStatus : uint8_t {
    NEW = 0,
    PARTIAL = 1,
    FILLED = 2,
    CANCELLED = 3,
    REJECTED = 4
};

struct Order {
    // Identifiers
    uint64_t order_id = 0;
    uint64_t client_order_id = 0;
    
    // Order details
    char symbol[8] = {0};        // Fixed-size, no allocation
    OrderSide side = OrderSide::BUY;
    OrderType type = OrderType::LIMIT;
    OrderStatus status = OrderStatus::NEW;
    
    // Price and quantity (integer to avoid floating point)
    int64_t price = 0;           // Price in cents (e.g., 10000 = $100.00)
    uint32_t quantity = 0;
    uint32_t filled_quantity = 0;
    uint32_t remaining_quantity = 0;
    
    // Timestamps (TSC cycles for nanosecond precision)
    uint64_t received_timestamp = 0;
    uint64_t matched_timestamp = 0;
    
    // Linked list pointers for price levels
    Order* prev = nullptr;
    Order* next = nullptr;
    
    // Pool management
    Order* pool_next = nullptr;
    
    // Helper methods
    bool is_buy() const { return side == OrderSide::BUY; }
    bool is_sell() const { return side == OrderSide::SELL; }
    bool is_limit() const { return type == OrderType::LIMIT; }
    bool is_filled() const { return remaining_quantity == 0; }
    bool is_active() const { 
        return status == OrderStatus::NEW || status == OrderStatus::PARTIAL; 
    }
    
    void fill(uint32_t fill_qty) {
        filled_quantity += fill_qty;
        remaining_quantity = quantity - filled_quantity;
        status = (remaining_quantity == 0) ? OrderStatus::FILLED : OrderStatus::PARTIAL;
    }
    
    void cancel() { status = OrderStatus::CANCELLED; }
    
    // Reset for pool reuse
    void reset() {
        order_id = 0;
        client_order_id = 0;
        memset(symbol, 0, sizeof(symbol));
        side = OrderSide::BUY;
        type = OrderType::LIMIT;
        status = OrderStatus::NEW;
        price = 0;
        quantity = 0;
        filled_quantity = 0;
        remaining_quantity = 0;
        received_timestamp = 0;
        matched_timestamp = 0;
        prev = nullptr;
        next = nullptr;
    }
};

}