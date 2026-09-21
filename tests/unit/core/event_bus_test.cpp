#include "ssim/core/event_bus.hpp"

#include <gtest/gtest.h>

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
}  // namespace ssim::core
