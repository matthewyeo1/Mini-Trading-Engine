#include "velox/sim/strategy/bots.hpp"
#include "velox/sim/strategy/bot_manager.hpp"

namespace velox {
namespace bot {

void TradingBot::submit_order(const Order& order) {
    if (m_manager) {
        m_manager->push_order(order);
    }
}

} 
} 