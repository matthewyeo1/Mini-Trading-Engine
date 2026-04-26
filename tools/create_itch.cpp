#include <fstream>
#include <vector>
#include <cstdint>
#include <iostream>

static uint64_t g_order_id = 1000;
static uint64_t next_id() { return ++g_order_id; }

void write_add_order(std::ofstream& file, uint64_t order_id,
                     const char* symbol, char side, int64_t price, uint32_t quantity) {
    std::vector<uint8_t> msg(35, 0);
    msg[0] = 0x00;
    msg[1] = 0x23;
    msg[2] = 'A';

    msg[3]=0x00; msg[4]=0x00; msg[5]=0x00; msg[6]=0x00;
    msg[7]=0x0B; msg[8]=0x8B; msg[9]=0x8B; msg[10]=0x00;

    for (int i = 0; i < 8; ++i)
        msg[11 + i] = (order_id >> (56 - i * 8)) & 0xFF;

    for (int i = 0; i < 6 && symbol[i]; ++i)
        msg[19 + i] = symbol[i];

    msg[25] = side;

    uint32_t price_raw = static_cast<uint32_t>(price);
    msg[26] = (price_raw >> 24) & 0xFF;
    msg[27] = (price_raw >> 16) & 0xFF;
    msg[28] = (price_raw >>  8) & 0xFF;
    msg[29] =  price_raw        & 0xFF;

    msg[30] = (quantity >> 24) & 0xFF;
    msg[31] = (quantity >> 16) & 0xFF;
    msg[32] = (quantity >>  8) & 0xFF;
    msg[33] =  quantity        & 0xFF;

    file.write(reinterpret_cast<char*>(msg.data()), msg.size());
}

void write_cancel_order(std::ofstream& file, uint64_t order_id) {
    std::vector<uint8_t> msg(23, 0);
    msg[0] = 0x00;
    msg[1] = 0x17;
    msg[2] = 'X';

    msg[3]=0x00; msg[4]=0x00; msg[5]=0x00; msg[6]=0x00;
    msg[7]=0x0B; msg[8]=0x8B; msg[9]=0x8B; msg[10]=0x00;

    for (int i = 0; i < 8; ++i)
        msg[11 + i] = (order_id >> (56 - i * 8)) & 0xFF;

    msg[19]=0x00; msg[20]=0x00; msg[21]=0x00; msg[22]=0x64;

    file.write(reinterpret_cast<char*>(msg.data()), msg.size());
}

int main() {
    system("mkdir test_data 2>nul");

    std::ofstream file("test_data/NASDAQ_ITCH50_sample.bin", std::ios::binary);
    if (!file) { std::cerr << "Failed to create file\n"; return 1; }

    // AAPL

    // S1: Simple full match
    write_add_order(file, 1001, "AAPL", 'B', 10000, 100);
    write_add_order(file, 1002, "AAPL", 'S',  9900, 100);

    // S2: Partial fill — sell only fills 30 of 50
    write_add_order(file, 1003, "AAPL", 'B', 10100,  50);
    write_add_order(file, 1004, "AAPL", 'S', 10000,  30);

    // S3: Sweep two bid levels
    write_add_order(file, 1005, "AAPL", 'B', 10200, 100);
    write_add_order(file, 1006, "AAPL", 'B', 10100,  50);
    write_add_order(file, 1007, "AAPL", 'S', 10000, 120);

    // S4: Cancel before sell arrives (MSFT block, but AAPL cancel here for parity)
    uint64_t aapl_cancel_id = next_id();
    write_add_order(file, aapl_cancel_id, "AAPL", 'B', 10050, 200);
    write_cancel_order(file, aapl_cancel_id);

    // S5: Partial fill remainder rests in book
    write_add_order(file, 2001, "AAPL", 'S', 10000, 100);
    write_add_order(file, 2002, "AAPL", 'B', 10100, 150);

    // S6: Deep book sweep — 5 ask levels, one large buy
    write_add_order(file, next_id(), "AAPL", 'S', 10000,  50);
    write_add_order(file, next_id(), "AAPL", 'S', 10010,  50);
    write_add_order(file, next_id(), "AAPL", 'S', 10020,  50);
    write_add_order(file, next_id(), "AAPL", 'S', 10030,  50);
    write_add_order(file, next_id(), "AAPL", 'S', 10040,  50);
    write_add_order(file, next_id(), "AAPL", 'B', 10050, 300);  // sweeps all 5

    // S7: No match — buy only, rests in book
    write_add_order(file, next_id(), "AAPL", 'B', 9800, 100);

    // MSFT

    // S1: Simple full match
    write_add_order(file, 1008, "MSFT", 'B', 20000, 100);
    write_cancel_order(file, 1008);                          // cancel before fill
    write_add_order(file, 1009, "MSFT", 'B', 20000,  50);
    write_add_order(file, 1010, "MSFT", 'S', 19900,  50);

    // S2: Partial fill — buy 80, only 60 available
    write_add_order(file, next_id(), "MSFT", 'S', 20100,  60);
    write_add_order(file, next_id(), "MSFT", 'B', 20200,  80);

    // S3: Multiple bids, one large sell sweeps
    write_add_order(file, next_id(), "MSFT", 'B', 20300, 100);
    write_add_order(file, next_id(), "MSFT", 'B', 20200,  75);
    write_add_order(file, next_id(), "MSFT", 'B', 20100,  50);
    write_add_order(file, next_id(), "MSFT", 'S', 20000, 200);  // sweeps top 2, partial on 3rd

    // S4: Cancel mid-book
    uint64_t msft_mid = next_id();
    write_add_order(file, next_id(), "MSFT", 'B', 20500, 100);  // best bid
    write_add_order(file, msft_mid,  "MSFT", 'B', 20400,  80);  // second level
    write_add_order(file, next_id(), "MSFT", 'B', 20300,  60);  // third level
    write_cancel_order(file, msft_mid);                          // remove middle level
    write_add_order(file, next_id(), "MSFT", 'S', 20200, 150);  // fills top + third

    // S5: No match — ask only rests
    write_add_order(file, next_id(), "MSFT", 'S', 21000, 200);

    // GOOGL

    // S1: Simple full match
    write_add_order(file, 1011, "GOOGL", 'B', 15000, 100);
    write_add_order(file, next_id(), "GOOGL", 'S', 14900, 100);

    // S2: Large buy sweeps multiple ask levels
    write_add_order(file, next_id(), "GOOGL", 'S', 15000,  80);
    write_add_order(file, next_id(), "GOOGL", 'S', 15050,  80);
    write_add_order(file, next_id(), "GOOGL", 'S', 15100,  80);
    write_add_order(file, next_id(), "GOOGL", 'B', 15200, 300);  // sweeps all 3 asks

    // S3: Alternating adds and cancels stress test
    uint64_t g1 = next_id(), g2 = next_id(), g3 = next_id();
    write_add_order(file, g1, "GOOGL", 'B', 15200, 100);
    write_add_order(file, g2, "GOOGL", 'B', 15100,  80);
    write_add_order(file, g3, "GOOGL", 'B', 15000,  60);
    write_cancel_order(file, g1);   // cancel best bid
    write_cancel_order(file, g3);   // cancel worst bid
    write_add_order(file, next_id(), "GOOGL", 'S', 14900, 80);  // matches g2 only

    // S4: Partial fill remainder rests
    write_add_order(file, next_id(), "GOOGL", 'S', 15000, 120);
    write_add_order(file, next_id(), "GOOGL", 'B', 15100, 200);  // 120 fills, 80 rests

    // S5: No match — bid too low
    write_add_order(file, next_id(), "GOOGL", 'B', 14000, 50);

    // AMZN

    // S1: Simple full match
    write_add_order(file, next_id(), "AMZN", 'B', 18000, 100);
    write_add_order(file, next_id(), "AMZN", 'S', 17900, 100);

    // S2: Two asks at same price (FIFO — first ask fills first)
    write_add_order(file, next_id(), "AMZN", 'S', 18100,  60);  // first in queue
    write_add_order(file, next_id(), "AMZN", 'S', 18100,  60);  // second in queue
    write_add_order(file, next_id(), "AMZN", 'B', 18200,  90);  // fills first, partial second

    // S3: Bid side deep book
    write_add_order(file, next_id(), "AMZN", 'B', 18500, 100);
    write_add_order(file, next_id(), "AMZN", 'B', 18400,  80);
    write_add_order(file, next_id(), "AMZN", 'B', 18300,  60);
    write_add_order(file, next_id(), "AMZN", 'B', 18200,  40);
    write_add_order(file, next_id(), "AMZN", 'S', 18000, 250);  // sweeps top 3, partial on 4th

    // S4: Cancel then refill same level
    uint64_t amzn_c = next_id();
    write_add_order(file, amzn_c,    "AMZN", 'B', 18000, 200);
    write_cancel_order(file, amzn_c);
    write_add_order(file, next_id(), "AMZN", 'B', 18000, 150);  // new order at same price
    write_add_order(file, next_id(), "AMZN", 'S', 17500, 150);  // should match new order

    // S5: No match — spread too wide
    write_add_order(file, next_id(), "AMZN", 'B', 17000,  50);
    write_add_order(file, next_id(), "AMZN", 'S', 19000,  50);

    // META

    // S1: Simple full match
    write_add_order(file, next_id(), "META", 'B', 35000, 100);
    write_add_order(file, next_id(), "META", 'S', 34900, 100);

    // S2: Partial fill on buy side
    write_add_order(file, next_id(), "META", 'B', 35100, 200);
    write_add_order(file, next_id(), "META", 'S', 35000, 120);  // 120 fills, 80 bid rests

    // S3: Aggressive sell sweeps deep bid book
    write_add_order(file, next_id(), "META", 'B', 35500, 100);
    write_add_order(file, next_id(), "META", 'B', 35400,  80);
    write_add_order(file, next_id(), "META", 'B', 35300,  60);
    write_add_order(file, next_id(), "META", 'B', 35200,  40);
    write_add_order(file, next_id(), "META", 'B', 35100,  20);
    write_add_order(file, next_id(), "META", 'S', 35000, 400);  // sweeps all 5 levels, 100 rests

    // S4: Cancel stress — add 5, cancel 3, fill remaining 2
    uint64_t m1=next_id(), m2=next_id(), m3=next_id(), m4=next_id(), m5=next_id();
    write_add_order(file, m1, "META", 'B', 35600, 100);
    write_add_order(file, m2, "META", 'B', 35500,  80);
    write_add_order(file, m3, "META", 'B', 35400,  60);
    write_add_order(file, m4, "META", 'B', 35300,  40);
    write_add_order(file, m5, "META", 'B', 35200,  20);
    write_cancel_order(file, m1);
    write_cancel_order(file, m3);
    write_cancel_order(file, m5);
    // m2 (80 @ 35500) and m4 (40 @ 35300) remain
    write_add_order(file, next_id(), "META", 'S', 35000, 120);  // fills m2 fully, m4 fully

    // S5: No match — ask only
    write_add_order(file, next_id(), "META", 'S', 36000, 300);

    // Cross-symbol stress — rapid interleaved orders across all symbols
    for (int i = 0; i < 10; ++i) {
        write_add_order(file, next_id(), "AAPL",  'B', 10000 + i * 5, 50);
        write_add_order(file, next_id(), "MSFT",  'B', 20000 + i * 5, 50);
        write_add_order(file, next_id(), "GOOGL", 'S', 15000 - i * 5, 50);
        write_add_order(file, next_id(), "AMZN",  'S', 18000 - i * 5, 50);
        write_add_order(file, next_id(), "META",  'B', 35000 + i * 5, 50);
    }
    // Matching sells/buys to close out the above
    write_add_order(file, next_id(), "AAPL",  'S',  9900, 500);
    write_add_order(file, next_id(), "MSFT",  'S', 19900, 500);
    write_add_order(file, next_id(), "GOOGL", 'B', 15100, 500);
    write_add_order(file, next_id(), "AMZN",  'B', 18100, 500);
    write_add_order(file, next_id(), "META",  'S', 34900, 500);

    file.close();
    std::cout << "Written test_data/NASDAQ_ITCH50_sample.bin\n";
    return 0;
}