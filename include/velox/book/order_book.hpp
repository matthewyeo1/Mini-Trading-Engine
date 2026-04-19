#pragma once
#include <atomic>
#include <cstdint>
#include "velox/matching/order.hpp"
#include "velox/book/price_level.hpp"
#include "lockfree/hashmap.hpp"

namespace velox {

class OrderBook {
public:
    explicit OrderBook(const char* symbol);
    ~OrderBook();
    
    // Core operations
    bool add_order(Order* order);
    bool cancel_order(uint64_t order_id);
    Order* match(Order* incoming_order);
    
    // Best prices
    int64_t best_bid() const { return m_best_bid.load(std::memory_order_acquire); }
    int64_t best_ask() const { return m_best_ask.load(std::memory_order_acquire); }
    
    // Statistics
    uint64_t sequence() const { return m_sequence.load(std::memory_order_acquire); }
    
private:
    char m_symbol[8];
    std::atomic<uint64_t> m_sequence{0};
    std::atomic<int64_t> m_best_bid{0};
    std::atomic<int64_t> m_best_ask{INT64_MAX};
    
    // Price levels (simplified - using arrays for now)
    static constexpr size_t MAX_LEVELS = 1000;
    PriceLevel* m_bid_levels[MAX_LEVELS];
    PriceLevel* m_ask_levels[MAX_LEVELS];
    size_t m_bid_count = 0;
    size_t m_ask_count = 0;
    
    void update_best_prices();
    PriceLevel* find_level(int64_t price, bool is_bid);
    void insert_level(PriceLevel* level, bool is_bid);
};

} 