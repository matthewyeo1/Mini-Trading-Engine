#pragma once
#include <memory>
#include <vector>
#include <unordered_map>
#include "velox/sim/strategy/bots.hpp"
#include "velox/book/book_snapshot.hpp"
#include "lockfree/spsc_queue.hpp"

namespace velox {
namespace bot {

class BotManager {
public:
    using OrderQueue = lockfree::SPSCQueue<Order, 65536>;
    
    BotManager();
    ~BotManager();
    
    // Register a bot (takes ownership)
    void register_bot(std::unique_ptr<TradingBot> bot);
    
    // Called by snapshot thread to distribute snapshots to bots
    void on_snapshot(const BookSnapshot& snapshot);
    
    // Called by matching engine thread to consume orders
    bool pop_order(Order& order);
    void push_order(const Order& order);
    
    // Statistics
    size_t bot_count() const { return m_bots.size(); }
    uint64_t orders_queued() const;
    
private:
    std::vector<std::unique_ptr<TradingBot>> m_bots;
    std::unordered_map<std::string, std::vector<TradingBot*>> m_bots_by_symbol;
    OrderQueue m_order_queue;
};

} 
} 