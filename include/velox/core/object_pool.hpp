#pragma once

#include "velox/matching/order.hpp"
#include "lockfree/pool.hpp"

namespace velox {

using OrderPool = lockfree::ObjectPool<Order, 100000>;

}