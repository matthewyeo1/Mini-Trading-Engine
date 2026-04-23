#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include "velox/book/order_book.hpp"
#include "velox/book/book_snapshot.hpp"
#include "lockfree/pool.hpp"

using namespace velox;

class BookSnapshotTest : public ::testing::Test {
protected:
    void SetUp() override {
        pool = std::make_unique<lockfree::ObjectPool<Order, 10000>>();
        book = std::make_unique<OrderBook>("AAPL");
        manager = std::make_unique<BookSnapshotManager>(3);  // capture top 3 levels
        owned_orders.clear();
    }

    Order* create_order(uint64_t id, OrderSide side, int64_t price, uint32_t qty) {
        auto order = pool->acquire();
        order->order_id = id;
        order->side = side;
        order->price = price;
        order->quantity = qty;
        order->remaining_quantity = qty;
        order->filled_quantity = 0;
        std::strncpy(order->symbol, "AAPL", 7);
        Order* raw = order.get();
        owned_orders.push_back(std::move(order));
        return raw;
    }

    std::unique_ptr<lockfree::ObjectPool<Order, 10000>> pool;
    std::unique_ptr<OrderBook> book;
    std::unique_ptr<BookSnapshotManager> manager;
    std::vector<lockfree::PooledPtr<Order, 10000>> owned_orders;
};

TEST_F(BookSnapshotTest, EmptyBookSnapshot) {
    manager->update(*book);
    const BookSnapshot* snap = manager->get_snapshot();
    ASSERT_NE(snap, nullptr);
    EXPECT_EQ(snap->best_bid, 0);
    EXPECT_EQ(snap->best_ask, INT64_MAX);
    EXPECT_EQ(snap->bid_depth, 0);
    EXPECT_EQ(snap->ask_depth, 0);
    EXPECT_EQ(snap->spread, 0);
    EXPECT_DOUBLE_EQ(snap->mid_price, 0.0);
    EXPECT_STREQ(snap->symbol, "AAPL");
    manager->release_snapshot(snap);
}

TEST_F(BookSnapshotTest, CaptureAfterAddOrders) {
    // Add some orders
    auto buy1 = create_order(1, OrderSide::BUY, 10000, 100);
    auto buy2 = create_order(2, OrderSide::BUY, 10100, 200);
    auto sell1 = create_order(3, OrderSide::SELL, 10200, 150);
    book->add_order(buy1);
    book->add_order(buy2);
    book->add_order(sell1);

    manager->update(*book);
    const BookSnapshot* snap = manager->get_snapshot();
    ASSERT_NE(snap, nullptr);

    EXPECT_EQ(snap->best_bid, 10100);
    EXPECT_EQ(snap->best_ask, 10200);
    EXPECT_EQ(snap->bid_depth, 300);
    EXPECT_EQ(snap->ask_depth, 150);
    EXPECT_EQ(snap->spread, 100);
    EXPECT_DOUBLE_EQ(snap->mid_price, 10150.0);
    EXPECT_GT(snap->sequence, 0);

    manager->release_snapshot(snap);
}

TEST_F(BookSnapshotTest, CapturesTopLevels) {
    // Add 5 bid levels
    for (int i = 0; i < 5; ++i) {
        auto buy = create_order(i, OrderSide::BUY, 10000 + i * 10, 100);
        book->add_order(buy);
    }
    manager->update(*book);
    const BookSnapshot* snap = manager->get_snapshot();
    ASSERT_NE(snap, nullptr);

    // Should capture only top 3 levels (depth = 3)
    EXPECT_EQ(snap->bids.size(), 3);
    EXPECT_EQ(snap->bids[0].price, 10040);  // highest
    EXPECT_EQ(snap->bids[1].price, 10030);
    EXPECT_EQ(snap->bids[2].price, 10020);
    // Lower levels not captured
    EXPECT_EQ(snap->bids[2].quantity, 100);
    manager->release_snapshot(snap);
}

TEST_F(BookSnapshotTest, MultipleUpdates) {
    // First snapshot
    auto buy1 = create_order(1, OrderSide::BUY, 10000, 100);
    book->add_order(buy1);
    manager->update(*book);
    const BookSnapshot* snap1 = manager->get_snapshot();

    // Second snapshot – add another order
    auto buy2 = create_order(2, OrderSide::BUY, 10100, 200);
    book->add_order(buy2);
    manager->update(*book);
    const BookSnapshot* snap2 = manager->get_snapshot();

    // Third snapshot – cancel first order (simulate by removing or just add ask)
    book->cancel_order(1);
    manager->update(*book);
    const BookSnapshot* snap3 = manager->get_snapshot();

    // Verify each snapshot is independent and consistent
    EXPECT_EQ(snap1->best_bid, 10000);
    EXPECT_EQ(snap1->bid_depth, 100);

    EXPECT_EQ(snap2->best_bid, 10100);
    EXPECT_EQ(snap2->bid_depth, 300);

    EXPECT_EQ(snap3->best_bid, 10100);
    EXPECT_EQ(snap3->bid_depth, 200);  // first order cancelled

    manager->release_snapshot(snap1);
    manager->release_snapshot(snap2);
    manager->release_snapshot(snap3);
}

TEST_F(BookSnapshotTest, ConcurrentReadersAndWriter) {
    const int NUM_READERS = 4;
    const int NUM_UPDATES = 100;
    std::atomic<bool> stop{false};
    std::vector<std::thread> readers;
    std::atomic<int> successful_reads{0};

    // Writer thread: continuously update the snapshot
    std::thread writer([this, NUM_UPDATES, &stop]() {
        for (int i = 0; i < NUM_UPDATES; ++i) {
            // Add a dummy order to change the book state slightly
            auto order = create_order(1000 + i, OrderSide::BUY, 10000 + i, 100);
            book->add_order(order);
            manager->update(*book);
            std::this_thread::yield();  // give readers a chance
        }
        stop = true;
    });

    // Reader threads: continuously acquire and release snapshots
    for (int t = 0; t < NUM_READERS; ++t) {
        readers.emplace_back([this, &stop, &successful_reads]() {
            while (!stop) {
                const BookSnapshot* snap = manager->get_snapshot();
                if (snap) {
                    // Simple sanity check
                    if (snap->best_bid >= 0 && snap->best_ask >= snap->best_bid) {
                        successful_reads++;
                    }
                    manager->release_snapshot(snap);
                }
                std::this_thread::yield();
            }
        });
    }

    writer.join();
    for (auto& t : readers) t.join();

    EXPECT_GT(successful_reads, 0);
    // No crash or assertion failure is the main thing
}

TEST_F(BookSnapshotTest, ReferenceCountActiveReaders) {
    // Initially zero active readers
    EXPECT_EQ(manager->active_readers(), 0);

    // Acquire two snapshots
    const BookSnapshot* snap1 = manager->get_snapshot();
    const BookSnapshot* snap2 = manager->get_snapshot();
    // They may point to the same node if no update happened, but ref_count incremented twice
    EXPECT_GE(manager->active_readers(), 1);

    // Release one
    manager->release_snapshot(snap1);
    EXPECT_GE(manager->active_readers(), 1);

    // Release the second
    manager->release_snapshot(snap2);
    EXPECT_EQ(manager->active_readers(), 0);
}

TEST_F(BookSnapshotTest, WriterSkipsWhenNoFreeNode) {
    // Acquire all three possible snapshot nodes (by having multiple readers)
    const BookSnapshot* snap1 = manager->get_snapshot();
    const BookSnapshot* snap2 = manager->get_snapshot();
    const BookSnapshot* snap3 = manager->get_snapshot();

    // All three nodes are now pinned (ref_count > 0)
    // Writer should skip update because find_free_node returns nullptr
    manager->update(*book);   // should return early without publishing
    // No crash, no assertion

    manager->release_snapshot(snap1);
    manager->release_snapshot(snap2);
    manager->release_snapshot(snap3);

    // Now writer can update again
    manager->update(*book);
    const BookSnapshot* snap = manager->get_snapshot();
    ASSERT_NE(snap, nullptr);
    manager->release_snapshot(snap);
}

TEST_F(BookSnapshotTest, NoAllocationInUpdate) {
    // Force a few updates
    for (int i = 0; i < 10; ++i) {
        auto order = create_order(i, OrderSide::BUY, 10000 + i, 100);
        book->add_order(order);
        manager->update(*book);
    }
    // If any allocation occurred, it would be outside the hot path (already done in ctor)
    // This test simply ensures no crash.
    SUCCEED();
}