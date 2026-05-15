#include "velox/sim/env/market_sim.hpp"
#include <thread>

namespace velox {
namespace env {

void MarketSimulator::start(std::vector<std::unique_ptr<SymbolEngine>>& engines, PriceCallback callback) {
    m_running = true;

    m_thread = std::thread([this, &engines, callback]() {
        // Price per symbol (random walk)
        std::unordered_map<std::string, int64_t> current_prices;

        // Initialize resting orders for each symbol before starting simulation
        for (auto& e : engines) {
            int64_t initial_price = get_initial_price(e->symbol());
            current_prices[e->symbol()] = initial_price;
            initialize_resting_orders(*e, initial_price);

            std::cout << "[SIM] " << e->symbol() << " initial price = " << initial_price << std::endl;
        }

        std::normal_distribution<double> delta_dist(0.0, m_volatility / 3.0);

        while (m_running) {
            auto start = std::chrono::steady_clock::now();

            for (auto& e : engines) {
                // Random walk
                double delta = delta_dist(m_gen);
                int64_t new_price = current_prices[e->symbol()] + static_cast<int64_t>(delta);
                if (new_price < 100) new_price = 100;
                if (new_price > 1000000) new_price = 1000000;
                current_prices[e->symbol()] = new_price;

                int64_t spread = m_spread_dist(m_gen);
                int64_t bid = new_price - spread / 2;
                int64_t ask = new_price + spread / 2;
                if (bid < 1) bid = 1;
                if (ask > 999999) ask = 999999;

                e->set_market_price(bid, ask);

                // Only update resting orders when price changes significantly
                static std::unordered_map<std::string, int64_t> last_update_price;
                if (std::abs(new_price - last_update_price[e->symbol()]) > 10) {
                    update_resting_orders(*e, bid, ask);  
                    last_update_price[e->symbol()] = new_price;
                }

                if (callback) callback(*e, bid, ask);
            }

            auto elapsed = std::chrono::steady_clock::now() - start;
            if (elapsed < m_tick_interval) {
                std::this_thread::sleep_for(m_tick_interval - elapsed);
            }
        }
    });
}

void MarketSimulator::stop() {
    m_running = false;

    if (m_thread.joinable()) m_thread.join();

    clear_resting_orders();
}

void MarketSimulator::initialize_resting_orders(SymbolEngine& engine, int64_t initial_price) {
    SymbolOrders orders;
    int64_t bid_price = initial_price - 10;
    int64_t ask_price = initial_price + 10;
    
    // Create resting bid order
    auto bid_order = m_order_pool.acquire();
    if (!bid_order) throw std::runtime_error("Pool exhausted during init");
    bid_order->order_id = ++m_next_order_id;
    bid_order->side = OrderSide::BUY;
    bid_order->price = bid_price;
    bid_order->quantity = m_resting_quantity;
    bid_order->remaining_quantity = m_resting_quantity;
    bid_order->filled_quantity = 0;
    bid_order->status = OrderStatus::NEW;
    std::strncpy(bid_order->symbol, engine.symbol(), 7);
    engine.get_order_book().add_order(bid_order.get());
    
    // Create resting ask order
    auto ask_order = m_order_pool.acquire();
    if (!bid_order) throw std::runtime_error("Pool exhausted during init");
    ask_order->order_id = ++m_next_order_id;
    ask_order->side = OrderSide::SELL;
    ask_order->price = ask_price;
    ask_order->quantity = m_resting_quantity;
    ask_order->remaining_quantity = m_resting_quantity;
    ask_order->filled_quantity = 0;
    ask_order->status = OrderStatus::NEW;
    std::strncpy(ask_order->symbol, engine.symbol(), 7);
    engine.get_order_book().add_order(ask_order.get());

    // Store active PooledPtrs (overwrites any previous)
    auto active = std::make_unique<ActiveOrders>();
    active->bid = std::move(bid_order);
    active->ask = std::move(ask_order);
    m_active_orders[engine.symbol()] = std::move(active);
    
    // Store for later updates
    orders.bid_order_id = m_next_order_id - 1;
    orders.ask_order_id = m_next_order_id;
    orders.current_bid_price = bid_price;
    orders.current_ask_price = ask_price;
    
    m_symbol_orders[engine.symbol()] = std::move(orders);
}

void MarketSimulator::update_resting_orders(SymbolEngine& engine, int64_t bid_price, int64_t ask_price) {
    auto it = m_symbol_orders.find(engine.symbol());
    if (it == m_symbol_orders.end()) return;
    
    auto& orders = it->second;
    
    // If prices haven't changed, nothing to do
    if (orders.current_bid_price == bid_price && orders.current_ask_price == ask_price) {
        return;
    }
    
    // Cancel old orders
    engine.get_order_book().cancel_order(orders.bid_order_id);
    engine.get_order_book().cancel_order(orders.ask_order_id);
    
    // Create new resting bid order
    auto bid_order = m_order_pool.acquire();
    if (!bid_order) {
        std::cerr << "ERROR: Pool exhausted. Aborting update.\n";
        return;
    }

    bid_order->order_id = ++m_next_order_id;
    bid_order->side = OrderSide::BUY;
    bid_order->price = bid_price;
    bid_order->quantity = m_resting_quantity;
    bid_order->remaining_quantity = m_resting_quantity;
    bid_order->filled_quantity = 0;
    bid_order->status = OrderStatus::NEW;
    std::strncpy(bid_order->symbol, engine.symbol(), 7);
    engine.get_order_book().add_order(bid_order.get());
    
    // Create new resting ask order
    auto ask_order = m_order_pool.acquire();
    if (!bid_order) {
        std::cerr << "ERROR: Pool exhausted. Aborting update.\n";
        return;
    }

    ask_order->order_id = ++m_next_order_id;
    ask_order->side = OrderSide::SELL;
    ask_order->price = ask_price;
    ask_order->quantity = m_resting_quantity;
    ask_order->remaining_quantity = m_resting_quantity;
    ask_order->filled_quantity = 0;
    ask_order->status = OrderStatus::NEW;
    std::strncpy(ask_order->symbol, engine.symbol(), 7);
    engine.get_order_book().add_order(ask_order.get());

    // Replace active orders (pooled pointers go out of scope here)
    auto active = std::make_unique<ActiveOrders>();
    active->bid = std::move(bid_order);
    active->ask = std::move(ask_order);
    m_active_orders[engine.symbol()] = std::move(active);
    
    // Update metadata
    orders.bid_order_id = m_next_order_id - 1;
    orders.ask_order_id = m_next_order_id;
    orders.current_bid_price = bid_price;
    orders.current_ask_price = ask_price;
    m_symbol_orders[engine.symbol()] = orders;
}

void MarketSimulator::clear_resting_orders() {
    m_active_orders.clear();
    m_symbol_orders.clear();
}

int64_t MarketSimulator::get_initial_price(const std::string& symbol) const {
    auto it = m_initial_prices.find(symbol);
    return (it != m_initial_prices.end()) ? it->second : m_default_initial_price;
}

}
}