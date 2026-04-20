#include "velox/book/order_book.hpp"
#include <algorithm>
#include <cstring>

namespace velox {

OrderBook::OrderBook(const char* symbol) {
    std::strncpy(m_symbol, symbol, 7);
    m_symbol[7] = '\0';
}

OrderBook::~OrderBook() {
    // Clean up price levels
    for (auto level : m_bid_levels) {
        delete level;
    }
    for (auto level : m_ask_levels) {
        delete level;
    }
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

    // Add order to price level
    level->add_order(order);

    // Update best prices and depth
    update_depth();
    update_best_prices();

    // Increment sequence number
    m_sequence.fetch_add(1, std::memory_order_release);

    return true;
}

bool OrderBook::cancel_order(uint64_t order_id) {
    // TODO: Implement order cancellation
    // Need to track orders by ID for O(1) cancellation
    (void)order_id;
    return false;
}

Order* OrderBook::match(Order* incoming_order) {
    if (!incoming_order || incoming_order->remaining_quantity == 0) {
        return incoming_order;
    }

    bool is_bid = incoming_order->is_buy();
    std::vector<PriceLevel*>& levels = is_bid ? m_ask_levels : m_bid_levels;

    // Match against opposite side price levels
    for (size_t i = 0; i < levels.size() && incoming_order->remaining_quantity > 0; ++i) {
        PriceLevel* level = levels[i];
        
        // Check if price is matchable
        if ((is_bid && level->price() > incoming_order->price) ||
            (!is_bid && level->price() < incoming_order->price)) {
            break; // No more matchable levels
        }
        
        // Match order at this price level
        Order* remaining = level->match_order(incoming_order);
        
        // Update depth and best prices
        update_depth();
        update_best_prices();
        
        // Remove price level if empty
        if (level->empty()) {
            remove_level(level, !is_bid);
            delete level;
            --i;
        }
        
        // Fully matched
        if (remaining == incoming_order) {
            break; 
        }
    }

    // If incoming order still has remaining quantity, add it into the book
    if (incoming_order->remaining_quantity > 0) {
        bool add_is_bid = incoming_order->is_buy();
        PriceLevel* level = find_level(incoming_order->price, add_is_bid);
        if (!level) {
            level = new PriceLevel(incoming_order->price);
            insert_level(level, add_is_bid);
        }
        level->add_order(incoming_order);
        update_depth();
        update_best_prices();
    }

    update_best_prices();
    m_sequence.fetch_add(1, std::memory_order_release);

    return (incoming_order->remaining_quantity > 0) ? incoming_order : nullptr;
}

PriceLevel* OrderBook::find_level(int64_t price, bool is_bid) {
    std::vector<PriceLevel*>& levels = is_bid ? m_bid_levels : m_ask_levels;
    
    for (auto* level : levels) {
        if (level->price() == price) {
            return level;
        }
    }
    return nullptr;
}

void OrderBook::insert_level(PriceLevel* level, bool is_bid) {
    std::vector<PriceLevel*>& levels = is_bid ? m_bid_levels : m_ask_levels;
    
    // Find insertion position (bids: high to low, asks: low to high)
    size_t pos = 0;
    while (pos < levels.size()) {
        if (is_bid && levels[pos]->price() < level->price()) break;
        if (!is_bid && levels[pos]->price() > level->price()) break;
        pos++;
    }
    
    levels.insert(levels.begin() + pos, level);
}

void OrderBook::remove_level(PriceLevel* level, bool is_bid) {
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