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
    
    //std::cout << "[BotManager] Found " << it->second.size() << " bots for " << snapshot.symbol << std::endl;
    
    for (auto* bot : it->second) {
        bot->on_snapshot(snapshot);
    }
}

void BotManager::push_order(const Order& order) {
    m_order_queue.push(order);
}

bool BotManager::pop_order(Order& order) {
    auto opt = m_order_queue.pop();

    if (opt.has_value()) {
        order = std::move(opt.value());
        return true;
    }
    return false;
}

uint64_t BotManager::orders_queued() const {
    return m_order_queue.size();
}

} 
}