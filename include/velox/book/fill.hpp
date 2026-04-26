#pragma once
#include <cstdint>
#include "velox/matching/order.hpp"

namespace velox {

struct Fill {
    Order* order;
    uint32_t quantity;
    int64_t price;
};

}