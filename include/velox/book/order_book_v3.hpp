#pragma once
#include <atomic>
#include <cstdint>
#include <vector>
#include <unordered_map>
#include "velox/matching/order.hpp"
#include "velox/book/price_level.hpp"

namespace velox {

class OrderBook {
public:
    explicit OrderBook(const char *symbol);
    ~OrderBook();

    bool add_order(Order *order);
    bool cancel_order(uint64_t order_id);
    Order *match(Order *incoming_order);

    int64_t best_bid() const { return m_best_bid.load(std::memory_order_acquire); }
    int64_t best_ask() const { return m_best_ask.load(std::memory_order_acquire); }
    uint32_t bid_depth() const { return m_bid_depth.load(std::memory_order_acquire); }
    uint32_t ask_depth() const { return m_ask_depth.load(std::memory_order_acquire); }
    uint64_t sequence() const { return m_sequence.load(std::memory_order_acquire); }

    size_t bid_levels() const { return m_bid_prices.size(); }
    size_t ask_levels() const { return m_ask_prices.size(); }

private:
    char m_symbol[8];

    std::atomic<int64_t> m_best_bid{0};
    std::atomic<int64_t> m_best_ask{INT64_MAX};
    std::atomic<uint32_t> m_bid_depth{0};
    std::atomic<uint32_t> m_ask_depth{0};
    std::atomic<uint64_t> m_sequence{0};

    // Sorted price vectors — bids high→low, asks low→high
    // Small, cache-friendly, binary-searched for insert/remove
    std::vector<int64_t> m_bid_prices;
    std::vector<int64_t> m_ask_prices;

    // O(1) price → level lookup (shared across both sides)
    std::unordered_map<int64_t, PriceLevel *> m_price_to_level;

    // O(1) order ID → location for cancel
    struct OrderLocation
    {
        int64_t price;
        bool is_bid;
        Order *order_ptr = nullptr;
    };
    std::unordered_map<uint64_t, OrderLocation> m_order_index;

    PriceLevel *find_level(int64_t price) const;
    void insert_price(int64_t price, bool is_bid);
    void remove_price(int64_t price, bool is_bid);
    void update_best_prices();
    void add_depth(bool is_bid, uint32_t qty);
    void sub_depth(bool is_bid, uint32_t qty);
};

}