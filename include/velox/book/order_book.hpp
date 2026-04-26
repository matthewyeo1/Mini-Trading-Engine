#pragma once
#include <atomic>
#include <cstring>
#include <vector>
#include <unordered_map>
#include "velox/matching/order.hpp"
#include "velox/book/price_level.hpp"
#include <iostream>

namespace velox {

class OrderBook {
public:
    explicit OrderBook(const char* symbol);
    ~OrderBook();
    
    // Core operations
    bool add_order(Order* order);
    bool cancel_order(uint64_t order_id);
    Order* match(Order* incoming_order);
    const std::vector<PriceLevel*>& get_bid_levels() const { return m_bid_levels; }
    const std::vector<PriceLevel*>& get_ask_levels() const { return m_ask_levels; }
    
    // Market data
    int64_t best_bid() const { return m_best_bid.load(std::memory_order_acquire); }
    int64_t best_ask() const { return m_best_ask.load(std::memory_order_acquire); }
    uint32_t bid_depth() const { return m_bid_depth.load(std::memory_order_acquire); }
    uint32_t ask_depth() const { return m_ask_depth.load(std::memory_order_acquire); }
    uint64_t sequence() const { return m_sequence.load(std::memory_order_acquire); }
    
    // Stats
    size_t bid_levels() const { return m_bid_levels.size(); }
    size_t ask_levels() const { return m_ask_levels.size(); }

    // Symbol
    const char* symbol() const { return m_symbol; }

private:
    // Stock ticker size
    char m_symbol[8]; 

    // Order book state (keep atomic binding for multithreading of each symbol)
    alignas(64) std::atomic<uint64_t> m_sequence{0};
    alignas(64) std::atomic<int64_t> m_best_bid{0};
    alignas(64) std::atomic<int64_t> m_best_ask{INT64_MAX};
    alignas(64) std::atomic<uint32_t> m_bid_depth{0};
    alignas(64) std::atomic<uint32_t> m_ask_depth{0};
    
    // Price levels 
    std::vector<PriceLevel*> m_bid_levels;  // Sorted high to low
    std::vector<PriceLevel*> m_ask_levels;  // Sorted low to high

    // Order ID to location mapping for O(1) cancellation
    struct OrderLocation {
        int64_t price;
        bool is_bid;
        Order* order_ptr;
    };

    std::unordered_map<uint64_t, OrderLocation> m_order_index;

    // O(1) price → level mapping for quick access during matching
    std::unordered_map<int64_t, PriceLevel*> m_price_to_level;
    
    PriceLevel* find_level(int64_t price, bool is_bid);
    void insert_level(PriceLevel* level, bool is_bid);
    void remove_level(PriceLevel* level, bool is_bid);
    void update_best_prices();
    void update_depth();
};

}