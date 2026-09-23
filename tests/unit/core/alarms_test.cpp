#include "ssim/core/alarms.hpp"

#include <gtest/gtest.h>

#include "ssim/core/events.hpp"

namespace ssim::core {
namespace {

TEST(AlarmManager, SetPublishesOnceOnRisingEdge) {
    EventBus bus;
    int set_count = 0;
    bus.subscribe<AlarmSet>([&](const AlarmSet& e) {
        ++set_count;
        EXPECT_EQ(e.alid, static_cast<int>(AlarmId::kFitQualityPoor));
    });
    AlarmManager alarms(bus);

    alarms.set(AlarmId::kFitQualityPoor, "rms too high");
    alarms.set(AlarmId::kFitQualityPoor, "rms too high");  // FR-ALM-2: no repeat

    EXPECT_EQ(set_count, 1);
    EXPECT_TRUE(alarms.is_active(AlarmId::kFitQualityPoor));
    EXPECT_TRUE(alarms.any_active());
}

TEST(AlarmManager, ClearPublishesOnceOnFallingEdge) {
    EventBus bus;
    int clear_count = 0;
    bus.subscribe<AlarmCleared>([&](const AlarmCleared&) { ++clear_count; });
    AlarmManager alarms(bus);

    alarms.set(AlarmId::kScanStall, "no samples");
    alarms.clear(AlarmId::kScanStall);
    alarms.clear(AlarmId::kScanStall);  // already inactive: no repeat

    EXPECT_EQ(clear_count, 1);
    EXPECT_FALSE(alarms.is_active(AlarmId::kScanStall));
    EXPECT_FALSE(alarms.any_active());
}

TEST(AlarmManager, ClearAllClearsEveryActiveAlarm) {
    EventBus bus;
    int clear_count = 0;
    bus.subscribe<AlarmCleared>([&](const AlarmCleared&) { ++clear_count; });
    AlarmManager alarms(bus);

    alarms.set(AlarmId::kScanStall, "a");
    alarms.set(AlarmId::kQueueOverflow, "b");
    alarms.clear_all();

    EXPECT_EQ(clear_count, 2);
    EXPECT_FALSE(alarms.any_active());
}

}  // namespace
}  // namespace ssim::core
