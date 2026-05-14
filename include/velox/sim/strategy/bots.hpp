#pragma once
#include "velox/matching/order.hpp"
#include "velox/book/book_snapshot.hpp"
#include <random>

namespace velox {
namespace bot {

class BotManager;

// Base Trading Bot Class
class TradingBot {
public:
    TradingBot(const std::string& name, const std::string& symbol)
        : m_name(name), m_symbol(symbol) {}

    virtual ~TradingBot() = default;
    
    virtual void on_snapshot(const BookSnapshot& snapshot) = 0;
    virtual void on_fill(const Order& order, uint32_t fill_qty, int64_t fill_price) {
        (void)order; (void)fill_qty; (void)fill_price;
    }
    
    const std::string& name() const { return m_name; }
    const std::string& symbol() const { return m_symbol; }
    
    void set_manager(BotManager* manager) { m_manager = manager; }
    
    void submit_order(const Order& order); 

    std::string m_symbol;
private:
    std::string m_name;
    BotManager* m_manager = nullptr;
};

// Market Maker Bot: provides liquidity by maintaining resting orders
class MarketMakerBot : public TradingBot {
public:
    MarketMakerBot(const std::string& name, const std::string& symbol,
                   int64_t base_price = 10000, int64_t spread = 100,
                   uint32_t quantity = 500)
        : TradingBot(name, symbol)
        , m_base_price(base_price)
        , m_spread(spread)
        , m_quantity(quantity) {}
    
    void on_snapshot(const BookSnapshot& snapshot) override {
        /*
        static int call_count = 0;
        if (call_count++ < 3) {
            std::cout << "[MarketMakerBot] on_snapshot called for " << snapshot.symbol 
                    << " best_bid=" << snapshot.best_bid 
                    << " best_ask=" << snapshot.best_ask << std::endl;
        }
        */

        static int update_count = 0;
        
        // Only check every 10 snapshots (every 100ms)
        if (++update_count % 10 != 0) return;
        
        // Check if we have resting orders already
        bool has_bid = (snapshot.best_bid >= m_base_price - m_spread / 2);
        bool has_ask = (snapshot.best_ask <= m_base_price + m_spread / 2);
        
        // If no bids, place a bid
        if (!has_bid) {
            Order bid;
            bid.order_id = ++m_order_id;
            bid.side = OrderSide::BUY;
            bid.price = m_base_price - m_spread / 2;
            bid.quantity = m_quantity;
            bid.remaining_quantity = m_quantity;
            bid.type = OrderType::LIMIT;
            std::strncpy(bid.symbol, m_symbol.c_str(), 7);
            submit_order(bid);
            
            // std::cout << "[MarketMaker " << name() << "] Placed BUY at " << bid.price << std::endl;
        }
        
        // If no asks, place an ask
        if (!has_ask) {
            Order ask;
            ask.order_id = ++m_order_id;
            ask.side = OrderSide::SELL;
            ask.price = m_base_price + m_spread / 2;
            ask.quantity = m_quantity;
            ask.remaining_quantity = m_quantity;
            ask.type = OrderType::LIMIT;
            std::strncpy(ask.symbol, m_symbol.c_str(), 7);
            submit_order(ask);
            
            // std::cout << "[MarketMaker " << name() << "] Placed SELL at " << ask.price << std::endl;
        }
        
        // Gradually adjust base price based on recent trades
        if (snapshot.mid_price > 0) {
            m_base_price = snapshot.mid_price;
        }
    }
    
    void on_fill(const Order& order, uint32_t fill_qty, int64_t fill_price) override {
        /*
        std::cout << "[MarketMaker " << name() << "] Filled " << fill_qty 
                  << " @ " << fill_price << std::endl;
        */
    }
    
private:
    int64_t m_base_price;
    int64_t m_spread;
    uint32_t m_quantity;
    uint64_t m_order_id = 50000;
};

// Spread Bot: trades based on bid-ask spread changes
class SpreadBot : public TradingBot {
public:
    SpreadBot(const std::string& name, const std::string& symbol,
              int64_t threshold = 5, uint32_t quantity = 100)
        : TradingBot(name, symbol)
        , m_threshold(threshold)
        , m_quantity(quantity) {}
    
    void on_snapshot(const BookSnapshot& snapshot) override {
        if (m_last_spread == 0) {
            m_last_spread = snapshot.spread;
            return;
        }
        
        int64_t spread_change = snapshot.spread - m_last_spread;
        
        if (spread_change > m_threshold) {
            // Spread widened → buy
            Order order;
            order.order_id = ++m_order_id;
            order.side = OrderSide::BUY;
            order.price = snapshot.best_ask;
            order.quantity = m_quantity;
            order.remaining_quantity = m_quantity;
            order.type = OrderType::LIMIT;
            std::strncpy(order.symbol, snapshot.symbol, 7);
            submit_order(order);
        }
        else if (spread_change < -m_threshold) {
            // Spread narrowed → sell
            Order order;
            order.order_id = ++m_order_id;
            order.side = OrderSide::SELL;
            order.price = snapshot.best_bid;
            order.quantity = m_quantity;
            order.remaining_quantity = m_quantity;
            order.type = OrderType::LIMIT;
            std::strncpy(order.symbol, snapshot.symbol, 7);
            submit_order(order);
        }
        
        m_last_spread = snapshot.spread;
    }
    
private:
    int64_t m_threshold;
    uint32_t m_quantity;
    int64_t m_last_spread = 0;
    uint64_t m_order_id = 10000;
};

// Random Walk Bot: random buy/sell decisions
class RandomWalkBot : public TradingBot {
public:
    RandomWalkBot(const std::string& name, const std::string& symbol,
                  double buy_prob = 0.3, double sell_prob = 0.2, uint32_t quantity = 100)
        : TradingBot(name, symbol)
        , m_buy_prob(buy_prob)
        , m_sell_prob(sell_prob)
        , m_quantity(quantity)
        , m_gen(m_rd())
        , m_dist(0.0, 1.0) {}
    
    void on_snapshot(const BookSnapshot& snapshot) override {
        double r = m_dist(m_gen);
        
        if (r < m_buy_prob) {
            Order order;
            order.order_id = ++m_order_id;
            order.side = OrderSide::BUY;
            order.price = snapshot.best_ask;
            order.quantity = m_quantity;
            order.remaining_quantity = m_quantity;
            order.type = OrderType::LIMIT;
            std::strncpy(order.symbol, snapshot.symbol, 7);
            /*
            std::cout << "[RandomWalkBot " << name() << "] SUBMITTING BUY order at " 
                  << order.price << std::endl; */
            submit_order(order);
        }
        else if (r < m_buy_prob + m_sell_prob) {
            Order order;
            order.order_id = ++m_order_id;
            order.side = OrderSide::SELL;
            order.price = snapshot.best_bid;
            order.quantity = m_quantity;
            order.remaining_quantity = m_quantity;
            order.type = OrderType::LIMIT;
            std::strncpy(order.symbol, snapshot.symbol, 7);
            submit_order(order);
        }
    }
    
private:
    double m_buy_prob;
    double m_sell_prob;
    uint32_t m_quantity;
    std::random_device m_rd;
    std::mt19937 m_gen;
    std::uniform_real_distribution<> m_dist;
    uint64_t m_order_id = 20000;
};

// Mean Reversion Bot: trades when price deviates from mean
class MeanReversionBot : public TradingBot {
public:
    MeanReversionBot(const std::string& name, const std::string& symbol,
                     int lookback = 20, double z_threshold = 2.0, uint32_t quantity = 100)
        : TradingBot(name, symbol)
        , m_lookback(lookback)
        , m_z_threshold(z_threshold)
        , m_quantity(quantity) {}
    
    void on_snapshot(const BookSnapshot& snapshot) override {
        // Record price
        m_prices.push_back(snapshot.mid_price);
        if (m_prices.size() > m_lookback) {
            m_prices.erase(m_prices.begin());
        }
        
        if (m_prices.size() < m_lookback) return;
        
        double mean = calculate_mean();
        double stddev = calculate_stddev(mean);
        
        if (stddev < 1e-6) return;
        
        double z_score = (snapshot.mid_price - mean) / stddev;
        
        if (z_score > m_z_threshold) {
            // Price too high → sell
            Order order;
            order.order_id = ++m_order_id;
            order.side = OrderSide::SELL;
            order.price = snapshot.best_bid;
            order.quantity = m_quantity;
            order.remaining_quantity = m_quantity;
            order.type = OrderType::LIMIT;
            std::strncpy(order.symbol, snapshot.symbol, 7);
            submit_order(order);
        }
        else if (z_score < -m_z_threshold) {
            // Price too low → buy
            Order order;
            order.order_id = ++m_order_id;
            order.side = OrderSide::BUY;
            order.price = snapshot.best_ask;
            order.quantity = m_quantity;
            order.remaining_quantity = m_quantity;
            order.type = OrderType::LIMIT;
            std::strncpy(order.symbol, snapshot.symbol, 7);
            submit_order(order);
        }
    }
    
private:
    double calculate_mean() {
        double sum = 0;
        for (double p : m_prices) sum += p;
        return sum / m_prices.size();
    }
    
    double calculate_stddev(double mean) {
        double sum_sq = 0;
        for (double p : m_prices) {
            double diff = p - mean;
            sum_sq += diff * diff;
        }
        return std::sqrt(sum_sq / m_prices.size());
    }
    
    int m_lookback;
    double m_z_threshold;
    uint32_t m_quantity;
    std::vector<double> m_prices;
    uint64_t m_order_id = 30000;
};

// Momentum Bot: Trades based on price momentum
class MomentumBot : public TradingBot {
public:
    MomentumBot(const std::string& name, const std::string& symbol,
                int64_t momentum_threshold = 15, uint32_t quantity = 100)
        : TradingBot(name, symbol)
        , m_momentum_threshold(momentum_threshold)
        , m_quantity(quantity) {}
    
    void on_snapshot(const BookSnapshot& snapshot) override {
        if (m_prev_price == 0) {
            m_prev_price = snapshot.mid_price;
            return;
        }
        
        int64_t change = snapshot.mid_price - m_prev_price;
        m_prev_price = snapshot.mid_price;
        
        if (change > m_momentum_threshold) {
            // Upward momentum → buy
            Order order;
            order.order_id = ++m_order_id;
            order.side = OrderSide::BUY;
            order.price = snapshot.best_ask;
            order.quantity = m_quantity;
            order.remaining_quantity = m_quantity;
            order.type = OrderType::LIMIT;
            std::strncpy(order.symbol, snapshot.symbol, 7);
            submit_order(order);
        }
        else if (change < -m_momentum_threshold) {
            // Downward momentum → sell
            Order order;
            order.order_id = ++m_order_id;
            order.side = OrderSide::SELL;
            order.price = snapshot.best_bid;
            order.quantity = m_quantity;
            order.remaining_quantity = m_quantity;
            order.type = OrderType::LIMIT;
            std::strncpy(order.symbol, snapshot.symbol, 7);
            submit_order(order);
        }
    }
    
private:
    int64_t m_momentum_threshold;
    uint32_t m_quantity;
    int64_t m_prev_price = 0;
    uint64_t m_order_id = 40000;
};

} 
} 