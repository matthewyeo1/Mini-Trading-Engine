#include <gtest/gtest.h>
#include "velox/core/symbol_engine.hpp"
#include "velox/risk/risk_manager.hpp"
#include "velox/gateway/execution_gateway.hpp"
#include "velox/risk/position_manager.hpp"
#include "lockfree/pool.hpp"

using namespace velox;

class IntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Shared components
        risk_mgr = std::make_unique<RiskManager>();
        gateway = std::make_unique<ExecutionGateway>();
        pos_mgr = std::make_unique<PositionManager>();

        // Add workers to gateway
        int num_workers = std::thread::hardware_concurrency();
        for (int i = 0; i < num_workers; ++i) {
            gateway->add_worker();
        }

        // Create engines for several symbols
        symbols = {"AAPL", "MSFT", "GOOGL"};
        for (const auto& sym : symbols) {
            engines.push_back(std::make_unique<SymbolEngine>(
                sym.c_str(), risk_mgr.get(), gateway.get(), pos_mgr.get()));
        }

        // Global order pool (can be per‑engine, but a single pool is fine for tests)
        order_pool = std::make_unique<lockfree::ObjectPool<Order, 100000>>();
    }

    Order* create_order(uint64_t id, OrderSide side, const char* symbol,
                        int64_t price, uint32_t qty, OrderType type = OrderType::LIMIT) {
        auto order = order_pool->acquire();
        order->order_id = id;
        order->client_order_id = id;
        order->side = side;
        order->price = price;
        order->quantity = qty;
        order->remaining_quantity = qty;
        order->filled_quantity = 0;
        order->type = type;
        order->status = OrderStatus::NEW;
        std::strncpy(order->symbol, symbol, 7);
        Order* raw = order.get();
        owned_orders.push_back(std::move(order));
        return raw;
    }

    void submit_order(Order* order) {
        std::cout << "[INTEGRATION_TEST] Submitting order ID=" << order->order_id 
              << " symbol=" << order->symbol << std::endl;

        // Find the correct engine for the symbol
        for (auto& eng : engines) {
            if (strcmp(eng->order_book().symbol(), order->symbol) == 0) {
                eng->on_market_order(order);
                return;
            }
        }
        FAIL() << "No engine for symbol: " << order->symbol;
    }

    void run_match_cycles() {
        for (auto& eng : engines) {
            eng->run_match_cycle();
        }
    }

    void run_match_cycles(int times) {
        for (int i = 0; i < times; ++i) {
            run_match_cycles();
        }
    }

    SymbolEngine* engine_for_symbol(const char* sym) {
        for (auto& eng : engines) {
            if (strcmp(eng->order_book().symbol(), sym) == 0)
                return eng.get();
        }
        return nullptr;
    }

    std::unique_ptr<RiskManager> risk_mgr;
    std::unique_ptr<ExecutionGateway> gateway;
    std::unique_ptr<PositionManager> pos_mgr;
    std::vector<std::unique_ptr<SymbolEngine>> engines;
    std::vector<std::string> symbols;
    std::unique_ptr<lockfree::ObjectPool<Order, 100000>> order_pool;
    std::vector<lockfree::PooledPtr<Order, 100000>> owned_orders;
};

TEST_F(IntegrationTest, SingleSymbolMatch) {
    auto buy = create_order(1, OrderSide::BUY, "AAPL", 10000, 100);
    auto sell = create_order(2, OrderSide::SELL, "AAPL", 9900, 100);

    submit_order(buy);
    submit_order(sell);
    run_match_cycles();

    auto stats = engine_for_symbol("AAPL")->get_stats();
    EXPECT_EQ(stats.orders_matched, 1);
    EXPECT_EQ(stats.orders_rejected, 0);
    EXPECT_EQ(stats.orders_partially_filled, 0);

    // Check final positions
    EXPECT_EQ(pos_mgr->get_position("AAPL"), 0);
    EXPECT_EQ(pos_mgr->get_realized_pnl("AAPL"), (9900 - 10000) * 100); // loss = -10000
}

TEST_F(IntegrationTest, TwoSymbolsIndependent) {
    // AAPL: buy and sell that cross
    auto aapl_buy = create_order(1, OrderSide::BUY, "AAPL", 10000, 50);
    auto aapl_sell = create_order(2, OrderSide::SELL, "AAPL", 9900, 50);
    submit_order(aapl_buy);
    submit_order(aapl_sell);

    // MSFT: buy and sell that cross
    auto msft_buy = create_order(3, OrderSide::BUY, "MSFT", 20000, 30);
    auto msft_sell = create_order(4, OrderSide::SELL, "MSFT", 19900, 30);
    submit_order(msft_buy);
    submit_order(msft_sell);

    run_match_cycles();

    EXPECT_EQ(engine_for_symbol("AAPL")->get_stats().orders_matched, 1);
    EXPECT_EQ(engine_for_symbol("MSFT")->get_stats().orders_matched, 1);
    EXPECT_EQ(pos_mgr->get_position("AAPL"), 0);
    EXPECT_EQ(pos_mgr->get_position("MSFT"), 0);
}

TEST_F(IntegrationTest, PartialFillRemainderResting) {
    auto sell = create_order(1, OrderSide::SELL, "AAPL", 10000, 50);
    submit_order(sell);
    run_match_cycles();

    auto buy = create_order(2, OrderSide::BUY, "AAPL", 10100, 100);
    submit_order(buy);
    run_match_cycles();

    auto stats = engine_for_symbol("AAPL")->get_stats();
    EXPECT_EQ(stats.orders_matched, 1);
    EXPECT_EQ(stats.orders_partially_filled, 1);
    EXPECT_EQ(sell->remaining_quantity, 0);
    EXPECT_EQ(buy->remaining_quantity, 50);

    // Now add another sell to match the remaining 50
    auto sell2 = create_order(3, OrderSide::SELL, "AAPL", 10000, 50);
    submit_order(sell2);
    run_match_cycles();

    EXPECT_EQ(buy->remaining_quantity, 0);
    EXPECT_EQ(sell2->remaining_quantity, 0);
    EXPECT_EQ(engine_for_symbol("AAPL")->get_stats().orders_matched, 2);
}

TEST_F(IntegrationTest, RiskRejection) {
    risk_mgr->set_position_limit("AAPL", 30);   // max 30 shares net
    auto buy = create_order(1, OrderSide::BUY, "AAPL", 10000, 100);
    submit_order(buy);
    run_match_cycles();

    auto stats = engine_for_symbol("AAPL")->get_stats();
    EXPECT_EQ(stats.orders_rejected, 1);
    EXPECT_EQ(buy->status, OrderStatus::REJECTED);
}

TEST_F(IntegrationTest, CircuitBreakerHalt) {
    risk_mgr->activate_circuit_breaker();

    auto buy = create_order(1, OrderSide::BUY, "AAPL", 10000, 10);
    submit_order(buy);
    run_match_cycles();

    auto stats = engine_for_symbol("AAPL")->get_stats();
    EXPECT_EQ(stats.orders_rejected, 1);
    EXPECT_EQ(buy->status, OrderStatus::REJECTED);
}

TEST_F(IntegrationTest, PositionTracking) {
    // Add a buy order (resting, not filled yet)
    auto buy = create_order(1, OrderSide::BUY, "AAPL", 10000, 100);
    submit_order(buy);
    run_match_cycles();
    
    // Position is 0 until a trade occurs
    EXPECT_EQ(pos_mgr->get_position("AAPL"), 0);
    EXPECT_EQ(pos_mgr->get_realized_pnl("AAPL"), 0);

    // Add a sell order that matches
    auto sell = create_order(2, OrderSide::SELL, "AAPL", 9900, 100);
    submit_order(sell);
    run_match_cycles();

    // After trade, position should be 0 (buy 100, sell 100)
    EXPECT_EQ(pos_mgr->get_position("AAPL"), 0);
    
    // Realized P&L: (9900 - 10000) * 100 = -10000
    EXPECT_EQ(pos_mgr->get_realized_pnl("AAPL"), (9900 - 10000) * 100);
}

TEST_F(IntegrationTest, MarketOrderMatch) {
    auto limit_sell = create_order(1, OrderSide::SELL, "AAPL", 10000, 100);
    submit_order(limit_sell);
    run_match_cycles();

    auto market_buy = create_order(2, OrderSide::BUY, "AAPL", 0, 60);
    market_buy->type = OrderType::MARKET;
    submit_order(market_buy);
    run_match_cycles();

    EXPECT_EQ(market_buy->remaining_quantity, 0);
    EXPECT_EQ(limit_sell->remaining_quantity, 40);
    EXPECT_EQ(engine_for_symbol("AAPL")->get_stats().orders_matched, 1);
}

TEST_F(IntegrationTest, CancelBeforeMatch) {
    auto buy = create_order(1, OrderSide::BUY, "AAPL", 10000, 100);
    submit_order(buy);
    run_match_cycles();

    bool cancelled = engine_for_symbol("AAPL")->cancel_order(1);
    EXPECT_TRUE(cancelled);
    EXPECT_EQ(buy->status, OrderStatus::CANCELLED);

    auto sell = create_order(2, OrderSide::SELL, "AAPL", 9900, 100);
    submit_order(sell);
    run_match_cycles();

    EXPECT_EQ(engine_for_symbol("AAPL")->get_stats().orders_matched, 0);
    EXPECT_EQ(sell->remaining_quantity, 100); // still resting
}

TEST_F(IntegrationTest, ManyOrdersNoMatch) {
    const int N = 1000;
    for (int i = 0; i < N; ++i) {
        auto buy = create_order(i, OrderSide::BUY, "AAPL", 10000 + i, 100);
        submit_order(buy);
    }
    run_match_cycles();

    auto stats = engine_for_symbol("AAPL")->get_stats();
    EXPECT_EQ(stats.orders_matched, 0);
    EXPECT_EQ(stats.orders_rejected, 0);
}