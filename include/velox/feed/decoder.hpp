#pragma once
#include <cstdint>
#include <functional>

namespace velox {

// Forward declaration
struct Order;

class Decoder {
public:
    using OrderCallback = std::function<void(Order*)>;
    
    Decoder();
    ~Decoder();
    
    // Decode ITCH/FIX message
    void decode(const char* data, size_t len, OrderCallback callback);
    
    // Statistics
    uint64_t decoded_count() const { return m_decoded_count; }
    
private:
    uint64_t m_decoded_count = 0;
};

}