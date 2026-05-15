#include "velox/sim/strategy/bot_manager.hpp"

namespace velox {
namespace bot {

BotManager::BotManager() = default;
BotManager::~BotManager() = default;

void BotManager::register_bot(std::unique_ptr<TradingBot> bot) {
    bot->set_manager(this);
    std::string symbol = bot->symbol();
    m_bots_by_symbol[symbol].push_back(bot.get());
    m_bots.push_back(std::move(bot));
}

/*
void BotManager::on_snapshot(const BookSnapshot& snapshot) {
    auto it = m_bots_by_symbol.find(snapshot.symbol);

    if (it == m_bots_by_symbol.end()) return;

    for (auto* bot : it->second) {
        bot->on_snapshot(snapshot);
    }
}
*/
void BotManager::on_snapshot(const BookSnapshot& snapshot) {
    auto it = m_bots_by_symbol.find(snapshot.symbol);
    if (it == m_bots_by_symbol.end()) {
        // std::cout << "[BotManager] No bots for symbol " << snapshot.symbol << std::endl;
        return;
    }
    
    // std::cout << "[BotManager] Found " << it->second.size() << " bots for " << snapshot.symbol << std::endl;
    
    for (auto* bot : it->second) {
        bot->on_snapshot(snapshot);
    }
}

void BotManager::push_order(const Order& order) {
    auto it = m_order_queues.find(std::string(order.symbol));
    if (it == m_order_queues.end()) {
        // Lazily create per-symbol queue
        auto q = std::make_unique<OrderQueue>();
        auto res = q.get();
        m_order_queues.emplace(std::string(order.symbol), std::move(q));
        m_order_queues[std::string(order.symbol)]->push(order);
        return;
    }
    it->second->push(order);
}

void BotManager::push_cancel(uint64_t order_id) {
    m_cancel_queue.push(order_id);
}

bool BotManager::pop_cancel(uint64_t& order_id) {
    auto opt = m_cancel_queue.pop();
    if (opt.has_value()) {
        order_id = opt.value();
        return true;
    }
    return false;
}

bool BotManager::pop_order_for_symbol(const std::string& symbol, Order& order) {
    auto it = m_order_queues.find(symbol);
    if (it == m_order_queues.end()) return false;

    auto opt = it->second->pop();
    if (opt.has_value()) {
        order = std::move(opt.value());
        return true;
    }
    return false;
}

uint64_t BotManager::orders_queued() const {
    uint64_t sum = 0;
    for (const auto& p : m_order_queues) sum += p.second->size();
    return sum + m_cancel_queue.size();
}

} 
}