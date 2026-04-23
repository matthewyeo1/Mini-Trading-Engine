#include "velox/feed/feed_handler.hpp"
#include <fstream>
#include <cstring>
#include <vector>

#ifdef _WIN32
    #include <winsock2.h>
    #pragma comment(lib, "ws2_32.lib")
#else
    #include <arpa/inet.h>
#endif

namespace velox {

FeedHandler::FeedHandler() = default;
FeedHandler::~FeedHandler() = default;

void FeedHandler::on_add_order(OrderCallback cb) { 
    m_on_add_order.push_back(std::move(cb)); 
}
void FeedHandler::on_cancel_order(CancelCallback cb) { 
    m_on_cancel_order.push_back(std::move(cb)); 
}
void FeedHandler::on_modify_order(ModifyCallback cb) { 
    m_on_modify_order.push_back(std::move(cb)); 
}
void FeedHandler::on_trade(TradeCallback cb) { 
    m_on_trade.push_back(std::move(cb)); 
}

void FeedHandler::clear_callbacks() {
    m_on_add_order.clear();
    m_on_cancel_order.clear();
    m_on_modify_order.clear();
    m_on_trade.clear();
}

uint64_t FeedHandler::parse_timestamp(const uint8_t* data) {
    uint64_t ts = 0;
    for (int i = 0; i < 8; ++i) {
        ts = (ts << 8) | data[i];
    }

    return ts;
}

uint64_t FeedHandler::parse_price(const uint8_t* data) {
    uint32_t price_raw = 0;
    for (int i = 0; i < 4; ++i) {
        price_raw = (price_raw << 8) | data[i];
    }
    return static_cast<uint64_t>(price_raw);
}

void FeedHandler::parse_add_order(const uint8_t* data, size_t len) {
    if (len < 35) {
        m_error_count++;
        return;
    }
    
    Order order;
    
    order.order_id = 0;
    for (int i = 0; i < 8; ++i) {
        order.order_id = (order.order_id << 8) | data[11 + i];
    }
    
    std::strncpy(order.symbol, reinterpret_cast<const char*>(data + 19), 6);
    order.symbol[6] = '\0';
    
    // Trim trailing spaces
    for (int i = 5; i >= 0; --i) {
        if (order.symbol[i] == ' ') {
            order.symbol[i] = '\0';
        } else {
            break;
        }
    }
    
    // Side: 'B' = Buy, 'S' = Sell
    char side_char = static_cast<char>(data[25]);
    order.side = (side_char == 'B') ? OrderSide::BUY : OrderSide::SELL;
    
    order.price = parse_price(data + 26);
    
    order.quantity = 0;
    for (int i = 0; i < 4; ++i) {
        order.quantity = (order.quantity << 8) | data[30 + i];
    }
    
    order.remaining_quantity = order.quantity;
    order.received_timestamp = parse_timestamp(data + 3);
    order.type = OrderType::LIMIT;
    order.status = OrderStatus::NEW;
    
    for (const auto& cb : m_on_add_order) {
        if (cb) cb(order);
    }
    
    m_message_count++;
}

void FeedHandler::parse_order_cancel(const uint8_t* data, size_t len) {
    if (len < 23) {
        m_error_count++;
        return;
    }
    
    uint64_t order_id = 0;
    for (int i = 0; i < 8; ++i) {
        order_id = (order_id << 8) | data[11 + i];
    }
    
    for (const auto& cb : m_on_cancel_order) {
        if (cb) cb(order_id);
    }
    
    m_message_count++;
}

void FeedHandler::parse_order_replace(const uint8_t* data, size_t len) {
    if (len < 35) {
        m_error_count++;
        return;
    }
    
    uint64_t order_id = 0;
    for (int i = 0; i < 8; ++i) {
        order_id = (order_id << 8) | data[11 + i];
    }
    
    uint32_t new_quantity = 0;
    for (int i = 0; i < 4; ++i) {
        new_quantity = (new_quantity << 8) | data[27 + i];
    }
    
    int64_t new_price = parse_price(data + 31);
    
    for (const auto& cb : m_on_modify_order) {
        if (cb) cb(order_id, new_quantity, new_price);
    }
    
    m_message_count++;
}

void FeedHandler::parse_order_executed(const uint8_t* data, size_t len) {
    if (len < 27) {
        m_error_count++;
        return;
    }
    
    uint64_t order_id = 0;
    for (int i = 0; i < 8; ++i) {
        order_id = (order_id << 8) | data[11 + i];
    }
    
    uint32_t executed_quantity = 0;
    for (int i = 0; i < 4; ++i) {
        executed_quantity = (executed_quantity << 8) | data[19 + i];
    }
    
    for (const auto& cb : m_on_trade) {
        if (cb) cb(order_id, executed_quantity, 0);
    }
    
    m_message_count++;
}

void FeedHandler::process(const char* data, size_t len) {
    if (!data || len == 0) return;
    
    const uint8_t* ptr = reinterpret_cast<const uint8_t*>(data);
    const uint8_t* end = ptr + len;
    
    while (ptr + 2 <= end) {
        uint16_t msg_len = (static_cast<uint16_t>(ptr[0]) << 8) | ptr[1];
        
        if (msg_len < 2 || ptr + msg_len > end) {
            m_error_count++;
            break;
        }
        
        char msg_type = static_cast<char>(ptr[2]);
        
        switch (msg_type) {
            case 'A':  
            case 'F':  
                parse_add_order(ptr, msg_len);
                break;
            case 'X': 
                parse_order_cancel(ptr, msg_len);
                break;
            case 'U':  
                parse_order_replace(ptr, msg_len);
                break;
            case 'E':  
            case 'C':  
                parse_order_executed(ptr, msg_len);
                break;
            default:
                break;
        }
        
        ptr += msg_len;
    }
}

void FeedHandler::process_file(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        return;
    }
    
    file.seekg(0, std::ios::end);
    size_t file_size = file.tellg();
    file.seekg(0, std::ios::beg);
    
    std::vector<char> buffer(file_size);
    file.read(buffer.data(), file_size);
    file.close();
    
    process(buffer.data(), buffer.size());
}

} 