#pragma once
#include <string>
#include <cstdint>
#include "velox/matching/order.hpp"

namespace velox {

struct ExecutionReport {
    uint64_t order_id = 0;
    uint64_t client_order_id = 0;
    char symbol[8] = {0};
    OrderSide side = OrderSide::BUY;
    OrderStatus status = OrderStatus::NEW;
    uint32_t executed_quantity = 0;
    int64_t executed_price = 0;
    uint64_t timestamp = 0;
    const char* reject_reason = nullptr;
};

class FixEncoder {
public:
    FixEncoder();
    ~FixEncoder();
    
    // Encode order to FIX 4.2 format
    std::string encode_new_order(const Order* order);
    std::string encode_cancel_order(uint64_t order_id, const char* symbol);
    
    // Decode execution report from FIX
    bool decode_execution_report(const std::string& fix_message, ExecutionReport& report);
    
    // FIX tag constants
    static constexpr int TAG_BEGIN_STRING = 8;
    static constexpr int TAG_BODY_LENGTH = 9;
    static constexpr int TAG_MSG_TYPE = 35;
    static constexpr int TAG_ORDER_ID = 37;
    static constexpr int TAG_CL_ORD_ID = 11;
    static constexpr int TAG_SYMBOL = 55;
    static constexpr int TAG_SIDE = 54;
    static constexpr int TAG_ORDER_QTY = 38;
    static constexpr int TAG_PRICE = 44;
    static constexpr int TAG_ORD_TYPE = 40;
    static constexpr int TAG_EXEC_QTY = 32;
    static constexpr int TAG_LAST_PX = 31;
    static constexpr int TAG_LAST_QTY = 32;
    
private:
    std::string m_sender_comp_id;
    std::string m_target_comp_id;
    int m_msg_seq_num = 1;
    
    std::string make_header(char msg_type);
    std::string make_trailer(const std::string& body);
    uint32_t calculate_checksum(const std::string& message);
};

}