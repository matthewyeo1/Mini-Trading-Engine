#pragma once
#include <atomic>
#include <thread>
#include <chrono>
#include <random>
#include <functional>
#include <unordered_map>
#include "velox/core/symbol_engine.hpp"
#include "lockfree/pool.hpp"

namespace velox {
namespace env {

    // Store PooledPtrs separately for active orders
    struct ActiveOrders {
        lockfree::PooledPtr<Order, 100000> bid;
        lockfree::PooledPtr<Order, 100000> ask;

        // Default constructor
        ActiveOrders() = default;
        
        // Move constructor
        ActiveOrders(ActiveOrders&& other) noexcept
            : bid(std::move(other.bid))
            , ask(std::move(other.ask)) {}
        
        // Move assignment
        ActiveOrders& operator=(ActiveOrders&& other) noexcept {
            if (this != &other) {
                bid = std::move(other.bid);
                ask = std::move(other.ask);
            }
            return *this;
        }
        
        // Constructor from moved PooledPtrs
        ActiveOrders(lockfree::PooledPtr<Order, 100000>&& b, 
                    lockfree::PooledPtr<Order, 100000>&& a) noexcept
            : bid(std::move(b))
            , ask(std::move(a)) {}
        
        // No copy
        ActiveOrders(const ActiveOrders&) = delete;
        ActiveOrders& operator=(const ActiveOrders&) = delete;
    };
    
class MarketSimulator {
public:
    // Polymorphic function wrapper for callback
    using PriceCallback = std::function<void(SymbolEngine&, int64_t bid, int64_t ask)>;

    MarketSimulator(double tick_intervals_ms = 100.0, 
                    const std::unordered_map<std::string, int64_t>& initial_prices = {},
                    int64_t volatility = 50,
                    int64_t min_spread = 5,
                    int64_t max_spread = 50,
                    uint32_t resting_quantity = 500) :
                m_tick_interval(std::chrono::milliseconds(static_cast<int>(tick_intervals_ms))),
                m_initial_prices(initial_prices),
                m_default_initial_price(10000),
                m_volatility(volatility),
                m_min_spread(min_spread),
                m_max_spread(max_spread),
                m_resting_quantity(resting_quantity),
                m_gen(std::random_device{}()),
                m_dist(0, volatility),
                m_spread_dist(min_spread, max_spread) {}

    ~MarketSimulator() { stop(); }

    // Core operations
    void start (std::vector<std::unique_ptr<SymbolEngine>>& engines, PriceCallback callback);
    void stop();

    // Resting orders operations
    void initialize_resting_orders(SymbolEngine& engine, int64_t initial_price);
    void update_resting_orders(SymbolEngine& engine, int64_t bid_price, int64_t ask_price);
    void clear_resting_orders();

    // Initialize initial price per share
    int64_t get_initial_price(const std::string& symbol) const;

private:
    std::chrono::milliseconds m_tick_interval;
    std::unordered_map<std::string, int64_t> m_initial_prices;
    int64_t m_default_initial_price;
    int64_t m_volatility;
    int64_t m_min_spread;
    int64_t m_max_spread;
    std::uniform_int_distribution<int64_t> m_spread_dist;
    int32_t m_resting_quantity;
    std::mt19937 m_gen;
    std::normal_distribution<double> m_dist;

    // Flags
    std::atomic<bool> m_running{false};

    // Env thread
    std::thread m_thread;

    // Resting orders for each symbol
    struct SymbolOrders {
        uint64_t bid_order_id = 0;
        uint64_t ask_order_id = 0;
        int64_t current_bid_price = 0;
        int64_t current_ask_price = 0;
    };

    // Per-symbol resting orders
    std::unordered_map<std::string, SymbolOrders> m_symbol_orders;

    // Track active orders
    std::unordered_map<std::string, std::unique_ptr<ActiveOrders>> m_active_orders;

    // Shared pool for simulator orders
    lockfree::ObjectPool<Order, 100000> m_order_pool;
    uint64_t m_next_order_id = 1000000;
};

}
}