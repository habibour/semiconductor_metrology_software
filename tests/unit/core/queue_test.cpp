#include "ssim/core/queue.hpp"

#include <gtest/gtest.h>

#include <future>
#include <thread>

namespace ssim::core {
namespace {

TEST(BoundedQueue, PushPopRoundTrip) {
    BoundedQueue<int> q(4, BackpressurePolicy::kBlock);
    EXPECT_TRUE(q.push(1));
    EXPECT_TRUE(q.push(2));
    EXPECT_EQ(q.size(), 2u);
    EXPECT_EQ(q.pop(), 1);
    EXPECT_EQ(q.pop(), 2);
    EXPECT_EQ(q.size(), 0u);
}

TEST(BoundedQueue, DropNewestPolicyDropsWhenFullAndCountsDrops) {
    BoundedQueue<int> q(2, BackpressurePolicy::kDropNewest);
    EXPECT_TRUE(q.push(1));
    EXPECT_TRUE(q.push(2));
    EXPECT_FALSE(q.push(3));  // dropped: queue full
    EXPECT_EQ(q.size(), 2u);
    EXPECT_EQ(q.dropped_count(), 1u);
    // The two items that made it in are unchanged (nothing evicted).
    EXPECT_EQ(q.pop(), 1);
    EXPECT_EQ(q.pop(), 2);
}

// A blocking pop() waits for a push() from another thread, without any
// sleep in this test: correctness is proven by the popped value, not by
// timing.
TEST(BoundedQueue, BlockingPopWaitsForPush) {
    BoundedQueue<int> q(1, BackpressurePolicy::kBlock);
    std::promise<int> popped;
    std::thread consumer([&q, &popped] { popped.set_value(*q.pop()); });

    q.push(42);
    consumer.join();

    EXPECT_EQ(popped.get_future().get(), 42);
}

// Under kBlock, push() waits for space to free up rather than dropping.
TEST(BoundedQueue, BlockingPushWaitsForSpace) {
    BoundedQueue<int> q(1, BackpressurePolicy::kBlock);
    ASSERT_TRUE(q.push(1));

    std::promise<bool> pushed_second;
    std::thread producer([&q, &pushed_second] { pushed_second.set_value(q.push(2)); });

    EXPECT_EQ(q.pop(), 1);  // frees space; producer's push() can now complete
    producer.join();

    EXPECT_TRUE(pushed_second.get_future().get());
    EXPECT_EQ(q.pop(), 2);
}

TEST(BoundedQueue, CloseWakesBlockedPopWithNullopt) {
    BoundedQueue<int> q(1, BackpressurePolicy::kBlock);
    std::promise<bool> got_value;
    std::thread consumer([&q, &got_value] { got_value.set_value(q.pop().has_value()); });

    q.close();
    consumer.join();

    EXPECT_FALSE(got_value.get_future().get());
}

TEST(BoundedQueue, PopDrainsRemainingItemsAfterClose) {
    BoundedQueue<int> q(4, BackpressurePolicy::kBlock);
    q.push(1);
    q.push(2);
    q.close();

    EXPECT_EQ(q.pop(), 1);
    EXPECT_EQ(q.pop(), 2);
    EXPECT_EQ(q.pop(), std::nullopt);
}

TEST(BoundedQueue, PushAfterCloseFails) {
    BoundedQueue<int> q(4, BackpressurePolicy::kBlock);
    q.close();
    EXPECT_FALSE(q.push(1));
}

}  // namespace
}  // namespace ssim::core
