#include <gtest/gtest.h>

#include <filesystem>
#include <unordered_map>
#include <vector>

#include "velox/sim/env/market_state_store.hpp"

TEST(MarketStateStore, ReadsAndWritesSymbolPrices) {
    const std::filesystem::path state_path = std::filesystem::temp_directory_path() /
        "velox_market_state_store_test.txt";

    const std::unordered_map<std::string, int64_t> expected = {
        {"AAPL", 17500},
        {"MSFT", 33000},
        {"GOOGL", 12500},
        {"AMZN", 13500},
        {"META", 30000},
    };

    velox::env::write_market_state(state_path.string(), expected);

    const std::vector<std::string> symbols = {"AAPL", "MSFT", "GOOGL", "AMZN", "META"};
    const auto loaded = velox::env::read_market_state(state_path.string(), symbols, {{"AAPL", 100}, {"MSFT", 200}});

    EXPECT_EQ(loaded.at("AAPL"), 17500);
    EXPECT_EQ(loaded.at("MSFT"), 33000);
    EXPECT_EQ(loaded.at("GOOGL"), 12500);
    EXPECT_EQ(loaded.at("AMZN"), 13500);
    EXPECT_EQ(loaded.at("META"), 30000);

    std::filesystem::remove(state_path);
}
