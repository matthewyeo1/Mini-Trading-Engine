#include "velox/book/book_snapshot.hpp"
#include "velox/book/order_book.hpp"
#include <cstring>
#include <climits>
#include <cstddef> 

namespace velox {

BookSnapshotManager::BookSnapshotManager(size_t depth)
    : m_depth(depth) {
    // Pre-size bid/ask vectors in all nodes so update() never allocates
    for (auto& node : m_nodes) {
        node.snapshot.bids.resize(depth);
        node.snapshot.asks.resize(depth);
        node.ref_count.store(0, std::memory_order_relaxed);
    }
    // Initialise current to node 0 with a zeroed snapshot
    m_current.store(&m_nodes[0], std::memory_order_release);
}

BookSnapshotManager::~BookSnapshotManager() {}

// Writer path (called by OrderBook)
void BookSnapshotManager::update(const OrderBook& book) {
    SnapshotNode* free_node = find_free_node();
    if (!free_node) return;  // all nodes in use — skip this update

    capture_snapshot(book, free_node->snapshot);

    // Publish atomically — readers will see the new snapshot on next acquire
    m_current.store(free_node, std::memory_order_release);
    m_update_count.fetch_add(1, std::memory_order_relaxed);
}

// Reader path
const BookSnapshot* BookSnapshotManager::get_snapshot() {
    while (true) {
        // Load current node
        SnapshotNode* node = m_current.load(std::memory_order_acquire);
        if (!node) return nullptr;

        // Increment ref count to pin the node
        node->ref_count.fetch_add(1, std::memory_order_acq_rel);

        // Validate: the writer may have swapped m_current between our load
        // and our increment. Re-check.
        if (node == m_current.load(std::memory_order_acquire)) {
            return &node->snapshot;
        }

        // Lost the race — undo the increment and retry
        node->ref_count.fetch_sub(1, std::memory_order_release);
    }
}

void BookSnapshotManager::release_snapshot(const BookSnapshot* snapshot) {
    if (!snapshot) return;

    // Recover the SnapshotNode from the BookSnapshot pointer.
    const SnapshotNode* cnode = reinterpret_cast<const SnapshotNode*>(
        reinterpret_cast<const char*>(snapshot)
        - offsetof(SnapshotNode, snapshot));

    SnapshotNode* node = const_cast<SnapshotNode*>(cnode);
    node->ref_count.fetch_sub(1, std::memory_order_release);
}

uint64_t BookSnapshotManager::active_readers() const {
    uint64_t total = 0;
    for (const auto& node : m_nodes)
        total += node.ref_count.load(std::memory_order_acquire);
    return total;
}

BookSnapshotManager::SnapshotNode* BookSnapshotManager::find_free_node() {
    SnapshotNode* current = m_current.load(std::memory_order_acquire);

    for (auto& node : m_nodes) {
        // Skip published node
        if (&node == current) continue;                         
        if (node.ref_count.load(std::memory_order_acquire) == 0)   // No readers
            return &node;
    }
    return nullptr;
}

void BookSnapshotManager::capture_snapshot(const OrderBook& book,
                                            BookSnapshot& snap) {
    std::strncpy(snap.symbol, book.symbol(), 7);
    snap.symbol[7] = '\0';
    snap.sequence   = book.sequence();
    snap.timestamp  = 0;  // caller can fill with rdtsc if needed
    snap.best_bid   = book.best_bid();
    snap.best_ask   = book.best_ask();
    snap.bid_depth  = book.bid_depth();
    snap.ask_depth  = book.ask_depth();
    snap.spread     = (snap.best_ask == INT64_MAX || snap.best_bid == 0)
                          ? 0
                          : snap.best_ask - snap.best_bid;
    snap.mid_price  = (snap.best_ask == INT64_MAX || snap.best_bid == 0)
                          ? 0.0
                          : static_cast<double>(snap.best_bid + snap.best_ask) / 2.0;

    // Capture bid levels — walk the sorted bid vector
    const auto& bid_levels = book.get_bid_levels();   
    size_t bid_count = 0;
    for (size_t i = 0; i < bid_levels.size() && bid_count < m_depth; ++i) {
        snap.bids[bid_count].price       = bid_levels[i]->price();
        snap.bids[bid_count].quantity    = bid_levels[i]->total_quantity();
        snap.bids[bid_count].order_count = 0;   
        ++bid_count;
    }
    // Zero out unused slots (vectors are pre-sized to m_depth)
    for (size_t i = bid_count; i < m_depth; ++i)
        snap.bids[i] = {};

    // Capture ask levels
    const auto& ask_levels = book.get_ask_levels();  
    size_t ask_count = 0;
    for (size_t i = 0; i < ask_levels.size() && ask_count < m_depth; ++i) {
        snap.asks[ask_count].price       = ask_levels[i]->price();
        snap.asks[ask_count].quantity    = ask_levels[i]->total_quantity();
        snap.asks[ask_count].order_count = 0;
        ++ask_count;
    }
    for (size_t i = ask_count; i < m_depth; ++i)
        snap.asks[i] = {};
}

} 