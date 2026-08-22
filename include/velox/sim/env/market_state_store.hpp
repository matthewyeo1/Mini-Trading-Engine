#pragma once

#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace velox {
namespace env {

using SymbolPriceMap = std::unordered_map<std::string, int64_t>;

inline SymbolPriceMap read_market_state(
    const std::string& file_path,
    const std::vector<std::string>& symbols,
    const SymbolPriceMap& defaults = {}) {
    SymbolPriceMap prices = defaults;

    std::ifstream input(file_path);
    if (!input.is_open()) {
        return prices;
    }

    std::string line;
    while (std::getline(input, line)) {
        if (line.empty()) {
            continue;
        }

        const auto delimiter = line.find('=');
        if (delimiter == std::string::npos) {
            continue;
        }

        const std::string symbol = line.substr(0, delimiter);
        const std::string raw_value = line.substr(delimiter + 1);
        if (symbol.empty()) {
            continue;
        }

        try {
            const int64_t parsed = std::stoll(raw_value);
            prices[symbol] = parsed;
        } catch (...) {
            // Ignore malformed lines and keep the default value.
        }
    }

    for (const auto& symbol : symbols) {
        if (prices.find(symbol) == prices.end()) {
            auto default_it = defaults.find(symbol);
            if (default_it != defaults.end()) {
                prices[symbol] = default_it->second;
            }
        }
    }

    return prices;
}

inline void write_market_state(const std::string& file_path, const SymbolPriceMap& prices) {
    std::ofstream output(file_path, std::ios::trunc);
    if (!output.is_open()) {
        return;
    }

    for (const auto& [symbol, price] : prices) {
        output << symbol << '=' << price << '\n';
    }

    output.flush();
}

inline std::string default_market_state_path() {
    return (std::filesystem::current_path() / "market_state.txt").string();
}

} // namespace env
} // namespace velox
