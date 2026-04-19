#include "velox/gateway/fix_encoder.hpp"
#include <sstream>
#include <iomanip>

namespace velox {

FixEncoder::FixEncoder() {
    // Default values - in production these come from config
    m_sender_comp_id = "SENDER";
    m_target_comp_id = "TARGET";
}

FixEncoder::~FixEncoder() = default;

std::string FixEncoder::encode_new_order(const Order* order) {
    std::string body;
    body += std::to_string(TAG_CL_ORD_ID) + "=" + std::to_string(order->client_order_id) + "\x01";
    body += std::to_string(TAG_SYMBOL) + "=" + std::string(order->symbol) + "\x01";
    body += std::to_string(TAG_SIDE) + "=" + (order->is_buy() ? "1" : "2") + "\x01";
    body += std::to_string(TAG_ORDER_QTY) + "=" + std::to_string(order->quantity) + "\x01";
    body += std::to_string(TAG_ORD_TYPE) + "=" + (order->is_limit() ? "2" : "1") + "\x01";
    
    if (order->is_limit()) {
        body += std::to_string(TAG_PRICE) + "=" + std::to_string(order->price) + "\x01";
    }
    
    return make_header('D') + body + make_trailer(body);
}

std::string FixEncoder::encode_cancel_order(uint64_t order_id, const char* symbol) {
    std::string body;
    body += std::to_string(TAG_ORDER_ID) + "=" + std::to_string(order_id) + "\x01";
    body += std::to_string(TAG_SYMBOL) + "=" + std::string(symbol) + "\x01";
    
    return make_header('F') + body + make_trailer(body);
}

bool FixEncoder::decode_execution_report(const std::string& fix_message, ExecutionReport& report) {
    // Simple parsing - in production use a proper FIX engine
    size_t pos = 0;
    while (pos < fix_message.length()) {
        size_t eq_pos = fix_message.find('=', pos);
        if (eq_pos == std::string::npos) break;
        
        int tag = std::stoi(fix_message.substr(pos, eq_pos - pos));
        size_t end_pos = fix_message.find('\x01', eq_pos + 1);
        if (end_pos == std::string::npos) break;
        
        std::string value = fix_message.substr(eq_pos + 1, end_pos - eq_pos - 1);
        
        switch (tag) {
            case TAG_ORDER_ID:
                report.order_id = std::stoull(value);
                break;
            case TAG_CL_ORD_ID:
                report.client_order_id = std::stoull(value);
                break;
            case TAG_EXEC_QTY:
                report.executed_quantity = std::stoul(value);
                break;
            case TAG_LAST_PX:
                report.executed_price = std::stoll(value);
                break;
        }
        
        pos = end_pos + 1;
    }
    
    return true;
}

std::string FixEncoder::make_header(char msg_type) {
    std::string header;
    header += std::to_string(TAG_BEGIN_STRING) + "=FIX.4.2\x01";
    header += std::to_string(TAG_MSG_TYPE) + "=" + msg_type + "\x01";
    header += "49=" + m_sender_comp_id + "\x01";
    header += "56=" + m_target_comp_id + "\x01";
    header += "34=" + std::to_string(m_msg_seq_num++) + "\x01";
    return header;
}

std::string FixEncoder::make_trailer(const std::string& body) {
    std::string trailer;
    uint32_t body_len = static_cast<uint32_t>(body.length());
    trailer += std::to_string(TAG_BODY_LENGTH) + "=" + std::to_string(body_len) + "\x01";
    
    std::string full_msg = trailer + body;
    uint32_t checksum = calculate_checksum(full_msg);
    
    trailer += "10=" + std::to_string(checksum) + "\x01";
    return trailer;
}

uint32_t FixEncoder::calculate_checksum(const std::string& message) {
    uint32_t sum = 0;
    for (char c : message) {
        sum += static_cast<uint8_t>(c);
    }
    return sum % 256;
}

} 