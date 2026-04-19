#pragma once
#include <cstdint>

namespace velox {

class FeedHandler {
public:
    FeedHandler();
    ~FeedHandler();
    
    // Process raw market data
    void process(const char* data, size_t len);
    
    // Get processed message count
    uint64_t message_count() const { return m_message_count; }
    
private:
    uint64_t m_message_count = 0;
};

}