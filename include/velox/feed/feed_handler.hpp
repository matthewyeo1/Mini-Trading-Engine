#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include "velox/matching/order.hpp"

namespace velox {

/**
 * NASDAQ ITCH 5.0 Message Type Identifiers
 * ---------------------------------------
 * * SYSTEM MESSAGES
 * 'S' - System Event (Market start/end, etc.)
 * * STOCK & ADMINISTRATIVE MESSAGES
 * 'R' - Stock Directory (Listing details, financial status)
 * 'H' - Stock Trading Action (Halt/Pause/Thaw status)
 * 'Y' - Reg SHO Short Sale Price Test Restricted Indicator
 * 'L' - Market Participant Position (Primary MM status)
 * 'V' - MWCB Decline Level Message (Market Wide Circuit Breaker)
 * 'W' - MWCB Status Message
 * 'K' - IPO Quoting Period Update
 * 'J' - LULD Auction Collar (Limit Up-Limit Down)
 * 'h' - Operational Halt (Exchange-level halts)
 * * ORDER MESSAGES
 * 'A' - Add Order - No MPID Attribution
 * 'F' - Add Order - With MPID Attribution
 * 'E' - Order Executed (Full or partial fill)
 * 'C' - Order Executed With Price (Fill at a price different than limit)
 * 'X' - Order Cancel (Partial reduction of shares)
 * 'D' - Order Delete (Full removal of order from book)
 * 'U' - Order Replace (Modify price or shares - changes Reference Number)
 * * TRADE MESSAGES
 * 'P' - Trade Message (Non-displayable orders match)
 * 'Q' - Cross Trade (Opening, Closing, or IPO crosses)
 * 'B' - Broken Trade / Modification (Trade nullification)
 * * IMBALANCE MESSAGES
 * 'I' - NOII (Net Order Imbalance Indicator)
 * * RETAIL PRICE IMPROVEMENT
 * 'N' - Retail Interest Indicator (RPI)
 */
enum class ITCHMessageType : uint8_t {
    SYSTEM_EVENT = 'S',        
    STOCK_DIRECTORY = 'R',     
    TRADING_ACTION = 'H',       
    REG_SHO = 'Y',  
    MARKET_PARTICIPANT_POS = 'L', 
    MWCB_DECLINE_LEVEL = 'V', 
    MWCB_STATUS = 'W', 
    IPO_QUOTING_UPDATE = 'K', 
    LULD_AUCTION_COLLAR = 'J', 
    OPERATIONAL_HALT = 'h',            
    ADD_ORDER = 'A',            
    ADD_ORDER_MPID = 'F',       
    ORDER_EXECUTED = 'E',       
    ORDER_EXECUTED_WITH_PRICE = 'C',   
    ORDER_CANCEL = 'X',         
    ORDER_DELETE = 'D',         
    ORDER_REPLACE = 'U',        
    TRADE = 'P',                
    CROSS_TRADE = 'Q',          
    BROKEN_TRADE = 'B',         
    NOII = 'I',                 
    RPII = 'N'                  
};

// Callback types
using OrderCallback = std::function<void(const Order&)>;
using CancelCallback = std::function<void(uint64_t order_id)>;
using ModifyCallback = std::function<void(uint64_t order_id, uint32_t new_quantity, int64_t new_price)>;
using TradeCallback = std::function<void(uint64_t order_id, uint32_t quantity, int64_t price)>;

class FeedHandler {
public:
    FeedHandler();
    ~FeedHandler();
    
    // Process raw market data
    void process(const char* data, size_t len);
    void process_file(const std::string& filename);

    // Callback registration
    void on_add_order(OrderCallback cb);
    void on_cancel_order(CancelCallback cb);
    void on_modify_order(ModifyCallback cb);
    void on_trade(TradeCallback cb);
    void clear_callbacks();
    
    // Get processed message count
    uint64_t message_count() const { return m_message_count; }
    uint64_t error_count() const { return m_error_count; }
    
private:
    uint64_t m_message_count = 0;
    uint64_t m_error_count = 0;

    // Callbacks
    std::vector<OrderCallback> m_on_add_order;
    std::vector<CancelCallback> m_on_cancel_order;
    std::vector<ModifyCallback> m_on_modify_order;
    std::vector<TradeCallback> m_on_trade;

    // Data parsing
    void parse_add_order(const uint8_t* data, size_t len);
    void parse_order_cancel(const uint8_t* data, size_t len);
    void parse_order_replace(const uint8_t* data, size_t len);
    void parse_order_executed(const uint8_t* data, size_t len);

    // Convert ITCH timestamp to nanoseconds
    uint64_t parse_timestamp(const uint8_t* data);
    uint64_t parse_price(const uint8_t* data);
};

}