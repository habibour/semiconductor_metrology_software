#include "ssim/core/event_bus.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <string>
#include <thread>

namespace ssim::core {
namespace {

struct TestEvent {
    int value;
};

struct OtherEvent {
    std::string text;
};

TEST(EventBus, SubscriberReceivesPublishedEvent) {
    EventBus bus;
    int received = 0;
    bus.subscribe<TestEvent>([&received](const TestEvent& e) { received = e.value; });

    bus.publish(TestEvent{42});

    EXPECT_EQ(received, 42);
}

TEST(EventBus, MultipleSubscribersAllReceiveTheEvent) {
    EventBus bus;
    int a = 0, b = 0;
    bus.subscribe<TestEvent>([&a](const TestEvent& e) { a = e.value; });
    bus.subscribe<TestEvent>([&b](const TestEvent& e) { b = e.value; });

    bus.publish(TestEvent{7});

    EXPECT_EQ(a, 7);
    EXPECT_EQ(b, 7);
}

TEST(EventBus, DifferentEventTypesDoNotCrossDeliver) {
    EventBus bus;
    bool test_event_received = false;
    bus.subscribe<TestEvent>(
        [&test_event_received](const TestEvent&) { test_event_received = true; });

    bus.publish(OtherEvent{"hello"});

    EXPECT_FALSE(test_event_received);
}

TEST(EventBus, UnsubscribeStopsDelivery) {
    EventBus bus;
    int received = 0;
    const auto id =
        bus.subscribe<TestEvent>([&received](const TestEvent& e) { received = e.value; });

    bus.unsubscribe(id);
    bus.publish(TestEvent{99});

    EXPECT_EQ(received, 0);
}

TEST(EventBus, PublishWithNoSubscribersDoesNothing) {
    EventBus bus;
    EXPECT_NO_THROW(bus.publish(TestEvent{1}));
}

}  // namespace
// Once unsubscribe() returns, the handler must not be running: the subscriber
// can be destroyed right after it. This was not true before, and it let a test
// destroy a recorder while a worker thread was still inside its handler.
TEST(EventBus, UnsubscribeWaitsForAHandlerThatIsAlreadyRunning) {
    EventBus bus;
    std::promise<void> entered;
    std::promise<void> release;
    const std::shared_future<void> release_future = release.get_future().share();
    std::atomic<bool> handler_finished{false};

    const auto id = bus.subscribe<TestEvent>([&](const TestEvent&) {
        entered.set_value();
        release_future.wait();  // blocks until the test lets it go
        handler_finished.store(true);
    });

    std::thread publisher([&] { bus.publish(TestEvent{1}); });
    entered.get_future().wait();  // the handler is now running on the publisher thread

    std::promise<void> about_to_unsubscribe;
    std::promise<void> unsubscribed;
    auto unsubscribed_future = unsubscribed.get_future();
    std::atomic<bool> finished_when_unsubscribe_returned{false};
    std::thread unsubscriber([&] {
        about_to_unsubscribe.set_value();
        bus.unsubscribe(id);
        finished_when_unsubscribe_returned.store(handler_finished.load());
        unsubscribed.set_value();
    });
    about_to_unsubscribe.get_future().wait();

    // The handler is still blocked, so unsubscribe must not return. One bounded
    // wait proves the negative; a bus that did not wait would return at once.
    EXPECT_EQ(unsubscribed_future.wait_for(std::chrono::milliseconds(100)),
              std::future_status::timeout);

    release.set_value();
    unsubscribed_future.wait();
    unsubscriber.join();
    publisher.join();
    EXPECT_TRUE(finished_when_unsubscribe_returned.load());
}

TEST(EventBus, NoHandlerStartsAfterUnsubscribeReturns) {
    EventBus bus;
    std::atomic<int> calls{0};
    std::atomic<bool> stop{false};
    std::thread publisher([&] {
        while (!stop.load()) bus.publish(TestEvent{1});
    });

    for (int round = 0; round < 200; ++round) {
        auto counter = std::make_shared<std::atomic<int>>(0);
        const auto id = bus.subscribe<TestEvent>([counter, &calls](const TestEvent&) {
            counter->fetch_add(1);
            calls.fetch_add(1);
        });
        std::this_thread::yield();
        bus.unsubscribe(id);
        const int after_unsubscribe = counter->load();
        std::this_thread::yield();
        EXPECT_EQ(counter->load(), after_unsubscribe) << "a handler ran after unsubscribe returned";
    }
    stop.store(true);
    publisher.join();
}

TEST(EventBus, AHandlerMayUnsubscribeItselfWithoutDeadlock) {
    EventBus bus;
    EventBus::SubscriptionId id = 0;
    int calls = 0;
    id = bus.subscribe<TestEvent>([&](const TestEvent&) {
        ++calls;
        bus.unsubscribe(id);  // must not wait for its own call
    });
    bus.publish(TestEvent{1});
    bus.publish(TestEvent{2});  // already gone
    EXPECT_EQ(calls, 1);
}

}  // namespace ssim::core
