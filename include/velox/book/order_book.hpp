#pragma once
#include <atomic>
#include <cstdint>
#include <vector>
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
    
    // Market data
    int64_t best_bid() const { return m_best_bid.load(std::memory_order_acquire); }
    int64_t best_ask() const { return m_best_ask.load(std::memory_order_acquire); }
    uint32_t bid_depth() const { return m_bid_depth.load(std::memory_order_acquire); }
    uint32_t ask_depth() const { return m_ask_depth.load(std::memory_order_acquire); }
    uint64_t sequence() const { return m_sequence.load(std::memory_order_acquire); }

    // Statistics
    size_t bid_levels() const { return m_bid_levels.size(); }
    size_t ask_levels() const { return m_ask_levels.size(); }
    
private:
    char m_symbol[8];
    std::atomic<int64_t> m_best_bid{0};
    std::atomic<int64_t> m_best_ask{INT64_MAX};
    std::atomic<uint32_t> m_bid_depth{0};
    std::atomic<uint32_t> m_ask_depth{0};
    std::atomic<uint64_t> m_sequence{0};
    
    // Price levels (simple vectors will be replaced with more efficient structures in production)
    static constexpr size_t MAX_LEVELS = 1000;
    std::vector<PriceLevel*> m_bid_levels;  // Sorted high to low
    std::vector<PriceLevel*> m_ask_levels;  // Sorted low to high
    size_t m_bid_count = 0;
    size_t m_ask_count = 0;
    
    void update_best_prices();

    PriceLevel* find_level(int64_t price, bool is_bid);

    void insert_level(PriceLevel* level, bool is_bid);

    void remove_level(PriceLevel* level, bool is_bid);

    void update_depth();
};

} 