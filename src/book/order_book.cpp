#include "velox/book/order_book.hpp"

namespace velox {

OrderBook::OrderBook(const char* symbol) {
    // TODO: Implement
}

bool OrderBook::add_order(Order* order) {
    // TODO: Implement
    return false;
}

bool OrderBook::cancel_order(uint64_t order_id) {
    // TODO: Implement
    return false;
}

Order* OrderBook::match(Order* incoming_order) {
    // TODO: Implement
    return nullptr;
}

}