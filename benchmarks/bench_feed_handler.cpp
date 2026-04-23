#include <benchmark/benchmark.h>
#include "velox/feed/feed_handler.hpp"
#include <vector>
#include <cstring>

using namespace velox;

// Helper to create a realistic ITCH Add Order message
static std::vector<char> create_itch_add_order(uint64_t order_id, 
                                                 const char* symbol,
                                                 char side,
                                                 int64_t price,
                                                 uint32_t quantity) {
    std::vector<char> msg(35, 0);
    
    // Length (2 bytes, big-endian)
    msg[0] = 0x00;
    msg[1] = 0x23;
    msg[2] = 'A';  // Add Order
    
    // Timestamp (8 bytes) - 13:00:00.000000000
    msg[3] = 0x00; msg[4] = 0x00; msg[5] = 0x00; msg[6] = 0x00;
    msg[7] = 0x0B; msg[8] = 0x8B; msg[9] = 0x8B; msg[10] = 0x00;
    
    // Order ID (8 bytes)
    for (int i = 0; i < 8; ++i) {
        msg[11 + i] = (order_id >> (56 - i * 8)) & 0xFF;
    }
    
    // Symbol (6 bytes)
    for (int i = 0; i < 6 && symbol[i]; ++i) {
        msg[19 + i] = symbol[i];
    }
    
    // Side
    msg[25] = side;
    
    // Price (4 bytes, price * 10000)
    uint32_t price_raw = static_cast<uint32_t>(price);
    msg[26] = (price_raw >> 24) & 0xFF;
    msg[27] = (price_raw >> 16) & 0xFF;
    msg[28] = (price_raw >> 8) & 0xFF;
    msg[29] = price_raw & 0xFF;
    
    // Quantity (4 bytes)
    msg[30] = (quantity >> 24) & 0xFF;
    msg[31] = (quantity >> 16) & 0xFF;
    msg[32] = (quantity >> 8) & 0xFF;
    msg[33] = quantity & 0xFF;
    
    return msg;
}

// Helper to create a realistic ITCH Cancel Order message
static std::vector<char> create_itch_cancel_order(uint64_t order_id) {
    std::vector<char> msg(23, 0);
    
    msg[0] = 0x00;
    msg[1] = 0x17;
    msg[2] = 'X';
    
    // Timestamp
    msg[3] = 0x00; msg[4] = 0x00; msg[5] = 0x00; msg[6] = 0x00;
    msg[7] = 0x0B; msg[8] = 0x8B; msg[9] = 0x8B; msg[10] = 0x00;
    
    // Order ID
    for (int i = 0; i < 8; ++i) {
        msg[11 + i] = (order_id >> (56 - i * 8)) & 0xFF;
    }
    
    // Cancelled quantity (4 bytes)
    msg[19] = 0x00; msg[20] = 0x00; msg[21] = 0x00; msg[22] = 0x64;
    
    return msg;
}

static void BM_FeedHandler_ParseAddOrder(benchmark::State& state) {
    FeedHandler handler;
    auto msg = create_itch_add_order(12345678, "AAPL", 'B', 1000000, 100);
    
    int count = 0;
    handler.on_add_order([&count](const Order&) { count++; });
    
    for (auto _ : state) {
        handler.process(msg.data(), msg.size());
        benchmark::DoNotOptimize(count);
    }
}
BENCHMARK(BM_FeedHandler_ParseAddOrder);

static void BM_FeedHandler_ParseCancelOrder(benchmark::State& state) {
    FeedHandler handler;
    auto msg = create_itch_cancel_order(12345678);
    
    int count = 0;
    handler.on_cancel_order([&count](uint64_t) { count++; });
    
    for (auto _ : state) {
        handler.process(msg.data(), msg.size());
        benchmark::DoNotOptimize(count);
    }
}
BENCHMARK(BM_FeedHandler_ParseCancelOrder);

static void BM_FeedHandler_BatchAddOrders(benchmark::State& state) {
    FeedHandler handler;
    const int N = state.range(0);
    
    // Pre-create messages
    std::vector<std::vector<char>> messages;
    messages.reserve(N);
    for (int i = 0; i < N; ++i) {
        messages.push_back(create_itch_add_order(i, "AAPL", 'B', 1000000 + i, 100));
    }
    
    int count = 0;
    handler.on_add_order([&count](const Order&) { count++; });
    
    for (auto _ : state) {
        count = 0;
        for (const auto& msg : messages) {
            handler.process(msg.data(), msg.size());
        }
        benchmark::DoNotOptimize(count);
    }
    state.SetItemsProcessed(state.iterations() * N);
}
BENCHMARK(BM_FeedHandler_BatchAddOrders)->Arg(10)->Arg(100)->Arg(1000);

static void BM_FeedHandler_MixedMessages(benchmark::State& state) {
    FeedHandler handler;
    const int N = state.range(0);
    
    // Create mixed messages (70% add, 20% cancel, 10% execute)
    std::vector<std::vector<char>> messages;
    messages.reserve(N);
    
    for (int i = 0; i < N; ++i) {
        int type = i % 100;
        if (type < 70) {
            messages.push_back(create_itch_add_order(i, "AAPL", 'B', 1000000 + i, 100));
        } else if (type < 90) {
            messages.push_back(create_itch_cancel_order(i));
        } else {
            // Execute message (simplified)
            std::vector<char> msg(27, 0);
            msg[0] = 0x00; msg[1] = 0x1B;
            msg[2] = 'E';
            msg[3] = 0x00; msg[4] = 0x00; msg[5] = 0x00; msg[6] = 0x00;
            msg[7] = 0x0B; msg[8] = 0x8B; msg[9] = 0x8B; msg[10] = 0x00;
            for (int j = 0; j < 8; ++j) {
                msg[11 + j] = (i >> (56 - j * 8)) & 0xFF;
            }
            msg[19] = 0x00; msg[20] = 0x00; msg[21] = 0x00; msg[22] = 0x64;
            messages.push_back(std::move(msg));
        }
    }
    
    int add_count = 0, cancel_count = 0;
    handler.on_add_order([&add_count](const Order&) { add_count++; });
    handler.on_cancel_order([&cancel_count](uint64_t) { cancel_count++; });
    
    for (auto _ : state) {
        add_count = 0;
        cancel_count = 0;
        for (const auto& msg : messages) {
            handler.process(msg.data(), msg.size());
        }
        benchmark::DoNotOptimize(add_count);
        benchmark::DoNotOptimize(cancel_count);
    }
    state.SetItemsProcessed(state.iterations() * N);
}
BENCHMARK(BM_FeedHandler_MixedMessages)->Arg(100)->Arg(1000);

static void BM_FeedHandler_Throughput(benchmark::State& state) {
    FeedHandler handler;
    const int N = 10000;
    
    // Create realistic batch of messages
    std::vector<std::vector<char>> messages;
    messages.reserve(N);
    for (int i = 0; i < N; ++i) {
        messages.push_back(create_itch_add_order(i, "AAPL", 'B', 1000000, 100));
    }
    
    handler.on_add_order([](const Order&) {});
    
    for (auto _ : state) {
        for (const auto& msg : messages) {
            handler.process(msg.data(), msg.size());
        }
        benchmark::DoNotOptimize(handler);
    }
    state.SetItemsProcessed(state.iterations() * N);
}
BENCHMARK(BM_FeedHandler_Throughput);

static void BM_FeedHandler_BufferProcessing(benchmark::State& state) {
    FeedHandler handler;
    const int NUM_MESSAGES = state.range(0);
    
    // Create a single buffer containing multiple messages
    std::vector<char> buffer;
    buffer.reserve(NUM_MESSAGES * 35);
    
    for (int i = 0; i < NUM_MESSAGES; ++i) {
        auto msg = create_itch_add_order(i, "AAPL", 'B', 1000000, 100);
        buffer.insert(buffer.end(), msg.begin(), msg.end());
    }
    
    handler.on_add_order([](const Order&) {});
    
    for (auto _ : state) {
        handler.process(buffer.data(), buffer.size());
        benchmark::DoNotOptimize(handler);
    }
    state.SetItemsProcessed(state.iterations() * NUM_MESSAGES);
}
BENCHMARK(BM_FeedHandler_BufferProcessing)->Arg(100)->Arg(1000)->Arg(10000);

static void BM_FeedHandler_SymbolHashing(benchmark::State& state) {
    FeedHandler handler;
    const char* symbols[] = {"AAPL", "MSFT", "GOOGL", "AMZN", "META", 
                              "TSLA", "NVDA", "JPM", "V", "WMT",
                              "JNJ", "PG", "UNH", "HD", "DIS"};
    
    std::vector<std::vector<char>> messages;
    for (int i = 0; i < 1000; ++i) {
        const char* sym = symbols[i % 15];
        messages.push_back(create_itch_add_order(i, sym, 'B', 1000000, 100));
    }
    
    handler.on_add_order([](const Order&) {});
    
    for (auto _ : state) {
        for (const auto& msg : messages) {
            handler.process(msg.data(), msg.size());
        }
        benchmark::DoNotOptimize(handler);
    }
    state.SetItemsProcessed(state.iterations() * 1000);
}
BENCHMARK(BM_FeedHandler_SymbolHashing);

static void BM_FeedHandler_MalformedMessage(benchmark::State& state) {
    FeedHandler handler;
    
    // Create malformed message (claims length 1000 but only 35 bytes)
    std::vector<char> msg(35, 0);
    msg[0] = 0x03;
    msg[1] = 0xE8;  // Length 1000 (invalid)
    msg[2] = 'A';
    
    for (auto _ : state) {
        handler.process(msg.data(), msg.size());
        benchmark::DoNotOptimize(handler);
    }
}
BENCHMARK(BM_FeedHandler_MalformedMessage);

BENCHMARK_MAIN();