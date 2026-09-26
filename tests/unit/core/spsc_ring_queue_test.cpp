#include "ssim/core/spsc_ring_queue.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <future>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace ssim::core {
namespace {

TEST(SpscRingQueue, PushPopRoundTripKeepsFifoOrder) {
    SpscRingQueue<int> q(4, BackpressurePolicy::kBlock);
    EXPECT_TRUE(q.push(1));
    EXPECT_TRUE(q.push(2));
    EXPECT_TRUE(q.push(3));
    EXPECT_EQ(q.size(), 3u);
    EXPECT_EQ(q.pop(), 1);
    EXPECT_EQ(q.pop(), 2);
    EXPECT_EQ(q.pop(), 3);
    EXPECT_EQ(q.size(), 0u);
}

// The ring rounds its storage up to a power of two but must still hold exactly
// the requested number of items.
TEST(SpscRingQueue, CapacityIsExactEvenWhenNotAPowerOfTwo) {
    SpscRingQueue<int> q(5, BackpressurePolicy::kDropNewest);
    for (int i = 0; i < 5; ++i) {
        EXPECT_TRUE(q.push(i)) << i;
    }
    EXPECT_FALSE(q.push(99));
    EXPECT_EQ(q.size(), 5u);
    EXPECT_EQ(q.dropped_count(), 1u);
    for (int i = 0; i < 5; ++i) {
        EXPECT_EQ(q.pop(), i);
    }
}

TEST(SpscRingQueue, DropNewestKeepsTheOldItemsAndCountsDrops) {
    SpscRingQueue<int> q(2, BackpressurePolicy::kDropNewest);
    EXPECT_TRUE(q.push(1));
    EXPECT_TRUE(q.push(2));
    EXPECT_FALSE(q.push(3));
    EXPECT_FALSE(q.push(4));
    EXPECT_EQ(q.dropped_count(), 2u);
    EXPECT_EQ(q.pop(), 1);
    EXPECT_EQ(q.pop(), 2);
    EXPECT_TRUE(q.push(5));  // space again after popping
    EXPECT_EQ(q.pop(), 5);
}

TEST(SpscRingQueue, IndicesWrapAroundTheRingManyTimes) {
    SpscRingQueue<int> q(3, BackpressurePolicy::kBlock);
    int next_in = 0;
    int next_out = 0;
    for (int round = 0; round < 1000; ++round) {
        const int burst = 1 + round % 3;
        for (int i = 0; i < burst; ++i) {
            ASSERT_TRUE(q.push(next_in++));
        }
        for (int i = 0; i < burst; ++i) {
            ASSERT_EQ(q.pop(), next_out++);
        }
    }
}

TEST(SpscRingQueue, WorksWithMoveOnlyTypes) {
    SpscRingQueue<std::unique_ptr<std::string>> q(2, BackpressurePolicy::kBlock);
    EXPECT_TRUE(q.push(std::make_unique<std::string>("first")));
    EXPECT_TRUE(q.push(std::make_unique<std::string>("second")));
    auto a = q.pop();
    ASSERT_TRUE(a.has_value());
    EXPECT_EQ(**a, "first");
    auto b = q.pop();
    ASSERT_TRUE(b.has_value());
    EXPECT_EQ(**b, "second");
}

// A push that fails because the ring is full must not consume the item.
TEST(SpscRingQueue, ADroppedItemIsNotMovedFrom) {
    SpscRingQueue<std::shared_ptr<int>> q(1, BackpressurePolicy::kDropNewest);
    EXPECT_TRUE(q.push(std::make_shared<int>(1)));
    auto rejected = std::make_shared<int>(2);
    const std::weak_ptr<int> watch = rejected;
    EXPECT_FALSE(q.push(rejected));  // copy: `rejected` still owns it
    EXPECT_EQ(watch.use_count(), 1);
}

// Items left in the queue are destroyed exactly once when the queue is.
TEST(SpscRingQueue, DestructorReleasesItemsStillQueued) {
    auto tracker = std::make_shared<int>(0);
    {
        SpscRingQueue<std::shared_ptr<int>> q(4, BackpressurePolicy::kBlock);
        EXPECT_TRUE(q.push(tracker));
        EXPECT_TRUE(q.push(tracker));
        EXPECT_TRUE(q.push(tracker));
        EXPECT_EQ(tracker.use_count(), 4);
        (void)q.pop();
        EXPECT_EQ(tracker.use_count(), 3);
    }
    EXPECT_EQ(tracker.use_count(), 1);
}

TEST(SpscRingQueue, PopReturnsQueuedItemsAfterCloseThenNullopt) {
    SpscRingQueue<int> q(4, BackpressurePolicy::kBlock);
    EXPECT_TRUE(q.push(1));
    EXPECT_TRUE(q.push(2));
    q.close();
    EXPECT_TRUE(q.closed());
    EXPECT_FALSE(q.push(3));
    EXPECT_EQ(q.pop(), 1);
    EXPECT_EQ(q.pop(), 2);
    EXPECT_EQ(q.pop(), std::nullopt);
    EXPECT_EQ(q.pop(), std::nullopt);
}

// No sleeps: correctness is shown by the value that comes out, not by timing.
TEST(SpscRingQueue, BlockingPopWaitsForPush) {
    SpscRingQueue<int> q(1, BackpressurePolicy::kBlock);
    std::promise<int> popped;
    std::thread consumer([&q, &popped] { popped.set_value(*q.pop()); });
    EXPECT_TRUE(q.push(42));
    EXPECT_EQ(popped.get_future().get(), 42);
    consumer.join();
}

TEST(SpscRingQueue, BlockingPushWaitsForSpace) {
    SpscRingQueue<int> q(1, BackpressurePolicy::kBlock);
    EXPECT_TRUE(q.push(1));
    std::promise<bool> pushed;
    std::thread producer([&q, &pushed] { pushed.set_value(q.push(2)); });
    EXPECT_EQ(q.pop(), 1);
    EXPECT_TRUE(pushed.get_future().get());
    producer.join();
    EXPECT_EQ(q.pop(), 2);
}

TEST(SpscRingQueue, CloseWakesABlockedConsumer) {
    SpscRingQueue<int> q(2, BackpressurePolicy::kBlock);
    std::promise<std::optional<int>> result;
    std::thread consumer([&q, &result] { result.set_value(q.pop()); });
    q.close();
    EXPECT_EQ(result.get_future().get(), std::nullopt);
    consumer.join();
}

TEST(SpscRingQueue, CloseWakesABlockedProducer) {
    SpscRingQueue<int> q(1, BackpressurePolicy::kBlock);
    EXPECT_TRUE(q.push(1));
    std::promise<bool> result;
    std::thread producer([&q, &result] { result.set_value(q.push(2)); });
    q.close();
    EXPECT_FALSE(result.get_future().get());
    producer.join();
}

// The ring's memory-order claims are only as good as this test: a producer and a
// consumer run flat out over a tiny ring (so both park and wake constantly) and
// every value must arrive exactly once, in order, with an intact payload. Run
// under ThreadSanitizer in CI.
TEST(SpscRingQueue, MillionItemStressKeepsOrderAndIntegrity) {
    constexpr std::uint64_t kItems = 1000000;
    SpscRingQueue<std::uint64_t> q(8, BackpressurePolicy::kBlock);

    std::thread producer([&q] {
        for (std::uint64_t i = 0; i < kItems; ++i) {
            ASSERT_TRUE(q.push(i * 2654435761u));
        }
        q.close();
    });

    std::uint64_t expected = 0;
    while (auto v = q.pop()) {
        ASSERT_EQ(*v, expected * 2654435761u) << "item " << expected;
        ++expected;
    }
    producer.join();
    EXPECT_EQ(expected, kItems);
}

// Same, with a payload that owns memory, so a torn or duplicated hand-over is
// caught by the sanitizers as a double free or a race, not only by a value check.
TEST(SpscRingQueue, StressWithHeapPayloadAndAnOccasionallyEmptyRing) {
    constexpr int kItems = 200000;
    SpscRingQueue<std::vector<int>> q(4, BackpressurePolicy::kBlock);

    std::thread producer([&q] {
        for (int i = 0; i < kItems; ++i) {
            // Every so often give the consumer time to drain and park.
            if (i % 5000 == 0) {
                std::this_thread::yield();
            }
            ASSERT_TRUE(q.push(std::vector<int>(1 + i % 7, i)));
        }
        q.close();
    });

    int expected = 0;
    while (auto v = q.pop()) {
        ASSERT_EQ(v->size(), static_cast<std::size_t>(1 + expected % 7));
        ASSERT_EQ(v->front(), expected);
        ASSERT_EQ(v->back(), expected);
        ++expected;
    }
    producer.join();
    EXPECT_EQ(expected, kItems);
}

TEST(SpscRingQueue, DropNewestStressAccountsForEveryItem) {
    constexpr std::uint64_t kItems = 300000;
    SpscRingQueue<std::uint64_t> q(16, BackpressurePolicy::kDropNewest);
    std::atomic<std::uint64_t> accepted{0};

    std::thread producer([&q, &accepted] {
        for (std::uint64_t i = 0; i < kItems; ++i) {
            if (q.push(i)) {
                ++accepted;
            }
        }
        q.close();
    });

    std::uint64_t received = 0;
    std::uint64_t last = 0;
    bool first = true;
    while (auto v = q.pop()) {
        if (!first) {
            ASSERT_GT(*v, last);  // survivors stay in order
        }
        first = false;
        last = *v;
        ++received;
    }
    producer.join();
    EXPECT_EQ(received, accepted.load());
    EXPECT_EQ(received + q.dropped_count(), kItems);
}

}  // namespace
}  // namespace ssim::core
