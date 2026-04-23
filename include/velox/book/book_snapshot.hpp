#pragma once
#include <atomic>
#include <cstring>
#include <vector>
#include "velox/matching/order.hpp"
#include "velox/book/order_book.hpp"

namespace velox {

class OrderBook;

struct BookLevel {
    int64_t price = 0;
    uint32_t quantity = 0;
    uint64_t order_count = 0;
};

struct BookSnapshot {
    char symbol[8] = {0};
    uint64_t sequence = 0;
    uint64_t timestamp = 0;
    
    int64_t best_bid = 0;
    int64_t best_ask = 0;
    uint32_t bid_depth = 0;
    uint32_t ask_depth = 0;
    int64_t spread = 0;
    double mid_price = 0.0;
    
    std::vector<BookLevel> bids;
    std::vector<BookLevel> asks;
    
    bool valid() const { return sequence > 0; }
};

class BookSnapshotManager {
public:
    explicit BookSnapshotManager(size_t depth = 10);
    ~BookSnapshotManager();
    
    // Called by Order Book (writer)
    void update(const OrderBook& book);
    
    // Called by readers: returns a snapshot that must be released
    const BookSnapshot* get_snapshot();
    
    // Must be called when reader is done with the snapshot
    void release_snapshot(const BookSnapshot* snapshot);
    
    // Statistics
    uint64_t update_count() const { return m_update_count.load(); }
    uint64_t active_readers() const;
    
private:
    struct SnapshotNode {
        BookSnapshot snapshot;
        std::atomic<uint64_t> ref_count{0};
        
        SnapshotNode() = default;
    };
    
    static constexpr size_t NUM_NODES = 3;  // RCU grace period needs at least 3
    SnapshotNode m_nodes[NUM_NODES];
    std::atomic<SnapshotNode*> m_current{nullptr};
    std::atomic<uint64_t> m_update_count{0};
    size_t m_depth;
    
    void capture_snapshot(const OrderBook& book, BookSnapshot& snap);
    SnapshotNode* find_free_node();
};

}