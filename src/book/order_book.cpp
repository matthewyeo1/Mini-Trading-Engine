#include "velox/book/order_book.hpp"
#include <algorithm>
#include <cstring>

namespace velox {

OrderBook::OrderBook(const char* symbol) {
    std::strncpy(m_symbol, symbol, 7);
    m_symbol[7] = '\0';
}

OrderBook::~OrderBook() {
    for (auto& [price, level] : m_price_to_level) delete level;
}

bool OrderBook::add_order(Order* order) {
    if (!order) return false;
    
    bool is_bid = order->is_buy();
    int64_t price = order->price;
    
    // Find or create price level
    PriceLevel* level = find_level(price, is_bid);
    if (!level) {
        level = new PriceLevel(price);
        insert_level(level, is_bid);
    }
    
    // Add order to the level
    level->add_order(order);

    // Index order for O(1) cancellation
    m_order_index[order->order_id] = {price, is_bid, order};
    
    // Update depth and sequence
    if (is_bid) {
        m_bid_depth.fetch_add(order->remaining_quantity, std::memory_order_release);
    } else {
        m_ask_depth.fetch_add(order->remaining_quantity, std::memory_order_release);
    }
    
    update_best_prices();
    m_sequence.fetch_add(1, std::memory_order_release);
    
    return true;
}

bool OrderBook::cancel_order(uint64_t order_id) {
    // O(1) hashmap lookup
    auto it = m_order_index.find(order_id);
    if (it == m_order_index.end()) {
        return false;
    }
    
    auto& loc = it->second;
    PriceLevel* level = find_level(loc.price, loc.is_bid);
    
    if (!level) {
        m_order_index.erase(it);
        return false;
    }
    
    // Remove from level
    level->remove_order(loc.order_ptr);
    
    // Update depth
    if (loc.is_bid) {
        m_bid_depth.fetch_sub(loc.order_ptr->remaining_quantity, std::memory_order_release);
    } else {
        m_ask_depth.fetch_sub(loc.order_ptr->remaining_quantity, std::memory_order_release);
    }
    
    // Mark as cancelled
    loc.order_ptr->cancel();
    
    // Remove level if empty
    if (level->empty()) {
        remove_level(level, loc.is_bid);
        delete level;
    }
    
    // Remove from index
    m_order_index.erase(it);
    
    update_best_prices();
    m_sequence.fetch_add(1, std::memory_order_release);
    
    return true;
}

Order* OrderBook::match(Order* incoming_order) {
    if (!incoming_order || incoming_order->remaining_quantity == 0)
        return incoming_order;

    bool is_bid = incoming_order->is_buy();
    std::vector<PriceLevel*>& levels = is_bid ? m_ask_levels : m_bid_levels;

    while (!levels.empty() && incoming_order->remaining_quantity > 0) {
        PriceLevel* level = levels.front();

        if ( is_bid && level->price() > incoming_order->price) break;
        if (!is_bid && level->price() < incoming_order->price) break;

        uint32_t before = level->total_quantity();
        level->match_order(incoming_order);
        uint32_t after = level->total_quantity();

        if (is_bid) m_ask_depth.fetch_sub(before - after, std::memory_order_release);
        else        m_bid_depth.fetch_sub(before - after, std::memory_order_release);

        if (level->empty()) {
            m_price_to_level.erase(level->price());
            levels.erase(levels.begin());
            delete level;
        }
    }

    // Rest unfilled quantity into the book
    if (incoming_order->remaining_quantity > 0) {
        PriceLevel* level = find_level(incoming_order->price, is_bid);
        if (!level) {
            level = new PriceLevel(incoming_order->price);
            insert_level(level, is_bid);
        }
        level->add_order(incoming_order);
        if (is_bid) m_bid_depth.fetch_add(incoming_order->remaining_quantity, std::memory_order_release);
        else        m_ask_depth.fetch_add(incoming_order->remaining_quantity, std::memory_order_release);
    }

    update_best_prices();
    m_sequence.fetch_add(1, std::memory_order_release);

    return incoming_order->remaining_quantity > 0 ? incoming_order : nullptr;
}

PriceLevel* OrderBook::find_level(int64_t price, bool is_bid) {
    (void)is_bid;  
    auto it = m_price_to_level.find(price);
    return it != m_price_to_level.end() ? it->second : nullptr;
}

void OrderBook::insert_level(PriceLevel* level, bool is_bid) {
    m_price_to_level[level->price()] = level;

    std::vector<PriceLevel*>& levels = is_bid ? m_bid_levels : m_ask_levels;
    
    // Insert while maintaining sorted order (O(logn))
    auto it = std::lower_bound(
        levels.begin(), levels.end(), level->price(),
        [is_bid](PriceLevel* existing, int64_t price) {
            return is_bid ? existing->price() > price   // bids: high→low
                          : existing->price() < price;  // asks: low→high
        });

    levels.insert(it, level);
}

void OrderBook::remove_level(PriceLevel* level, bool is_bid) {
    m_price_to_level.erase(level->price());

    std::vector<PriceLevel*>& levels = is_bid ? m_bid_levels : m_ask_levels;
    
    auto it = std::find(levels.begin(), levels.end(), level);
    if (it != levels.end()) {
        levels.erase(it);
    }
}

void OrderBook::update_best_prices() {
    if (!m_bid_levels.empty()) {
        m_best_bid.store(m_bid_levels[0]->price(), std::memory_order_release);
    } else {
        m_best_bid.store(0, std::memory_order_release);
    }
    
    if (!m_ask_levels.empty()) {
        m_best_ask.store(m_ask_levels[0]->price(), std::memory_order_release);
    } else {
        m_best_ask.store(INT64_MAX, std::memory_order_release);
    }
}

void OrderBook::update_depth() {
    uint32_t bid_depth = 0;
    for (auto* level : m_bid_levels) {
        bid_depth += level->total_quantity();
    }
    m_bid_depth.store(bid_depth, std::memory_order_release);
    
    uint32_t ask_depth = 0;
    for (auto* level : m_ask_levels) {
        ask_depth += level->total_quantity();
    }
    m_ask_depth.store(ask_depth, std::memory_order_release);
}

}