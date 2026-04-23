#include <fstream>
#include <vector>
#include <cstring>

int main() {
    std::ofstream file("test_data/mock_ITCH_sample.bin", std::ios::binary);
    
    // Create a simple Add Order message
    std::vector<uint8_t> msg(35, 0);
    msg[0] = 0x00;
    msg[1] = 0x23;  // Length 35
    msg[2] = 'A';   // Add Order
    
    // Timestamp (13:00:00.000000000)
    msg[3] = 0x00;
    msg[4] = 0x00;
    msg[5] = 0x00;
    msg[6] = 0x00;
    msg[7] = 0x0B;
    msg[8] = 0x8B;
    msg[9] = 0x8B;
    msg[10] = 0x00;
    
    // Order ID (12345678)
    msg[11] = 0x12;
    msg[12] = 0x34;
    msg[13] = 0x56;
    msg[14] = 0x78;
    msg[15] = 0x00;
    msg[16] = 0x00;
    msg[17] = 0x00;
    msg[18] = 0x00;
    
    // Symbol (AAPL)
    msg[19] = 'A';
    msg[20] = 'A';
    msg[21] = 'P';
    msg[22] = 'L';
    msg[23] = ' ';
    msg[24] = ' ';
    
    // Side (Buy)
    msg[25] = 'B';
    
    // Price ($100.00 = 1,000,000)
    msg[26] = 0x00;
    msg[27] = 0x0F;
    msg[28] = 0x42;
    msg[29] = 0x40;
    
    // Quantity (100)
    msg[30] = 0x00;
    msg[31] = 0x00;
    msg[32] = 0x00;
    msg[33] = 0x64;
    
    file.write(reinterpret_cast<char*>(msg.data()), msg.size());
    file.close();
    
    return 0;
}