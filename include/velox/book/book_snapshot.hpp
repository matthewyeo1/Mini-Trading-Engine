#pragma once
#include <cstdint>
#include <atomic>

namespace velox {

// Minimal BookSnapshot type used by BookSnapshotManager.
// The full definition can be extended as needed by the order book implementation.
class BookSnapshot {
public:
    BookSnapshot();
    ~BookSnapshot() = default;
    // snapshot data members go here (left intentionally minimal)
};


class BookSnapshotManager {
public:
    BookSnapshotManager();
    ~BookSnapshotManager();
    
    // RCU-protected snapshot access
    const BookSnapshot* get_snapshot() const;
    void update_snapshot(const BookSnapshot& snapshot);
    
private:
    struct SnapshotNode {
        BookSnapshot snapshot;
        std::atomic<SnapshotNode*> next;
    };
    
    std::atomic<SnapshotNode*> current;
};

}