#include <fstream>
#include <vector>
#include <cstdint>
#include <iostream>

void write_add_order(std::ofstream& file, uint64_t order_id, 
                     const char* symbol, char side, int64_t price, uint32_t quantity) {
    std::vector<uint8_t> msg(35, 0);
    msg[0] = 0x00;
    msg[1] = 0x23;          // length 35
    msg[2] = 'A';           // Add Order

    // Timestamp (13:00:00.000000000)
    msg[3] = 0x00; msg[4] = 0x00; msg[5] = 0x00; msg[6] = 0x00;
    msg[7] = 0x0B; msg[8] = 0x8B; msg[9] = 0x8B; msg[10] = 0x00;

    // Order ID (big‑endian)
    for (int i = 0; i < 8; ++i) {
        msg[11 + i] = (order_id >> (56 - i * 8)) & 0xFF;
    }

    // Symbol (6 bytes)
    for (int i = 0; i < 6 && symbol[i]; ++i) {
        msg[19 + i] = symbol[i];
    }

    // Side
    msg[25] = side;

    // Price (big‑endian, price * 10000)
    uint32_t price_raw = static_cast<uint32_t>(price);
    msg[26] = (price_raw >> 24) & 0xFF;
    msg[27] = (price_raw >> 16) & 0xFF;
    msg[28] = (price_raw >> 8) & 0xFF;
    msg[29] = price_raw & 0xFF;

    // Quantity (big‑endian)
    msg[30] = (quantity >> 24) & 0xFF;
    msg[31] = (quantity >> 16) & 0xFF;
    msg[32] = (quantity >> 8) & 0xFF;
    msg[33] = quantity & 0xFF;

    file.write(reinterpret_cast<char*>(msg.data()), msg.size());
}

int main() {
    // Ensure the output directory exists
    system("mkdir test_data 2>nul");

    std::ofstream file("test_data/NASDAQ_ITCH50_sample.bin", std::ios::binary);
    if (!file) {
        std::cerr << "Failed to create file\n";
        return 1;
    }

    // 1) Buy order for AAPL at $100.00 (10000 cents), quantity 100
    write_add_order(file, 1001, "AAPL", 'B', 10000, 100);

    // 2) Sell order for AAPL at $99.00 (9900 cents), quantity 100  (crosses!)
    write_add_order(file, 1002, "AAPL", 'S', 9900, 100);

    file.close();
    std::cout << "Generated test_data/NASDAQ_ITCH50_sample.bin with one crossing pair.\n";
    return 0;
}