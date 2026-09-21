#include "ssim/core/clock.hpp"

#include <gtest/gtest.h>

namespace ssim::core {
namespace {

TEST(FakeClock, StartsAtEpoch) {
    FakeClock clock;
    EXPECT_EQ(clock.monotonic_now(), std::chrono::steady_clock::time_point{});
    EXPECT_EQ(clock.wall_now(), std::chrono::system_clock::time_point{});
}

TEST(FakeClock, AdvanceMovesBothClocksByTheSameDelta) {
    FakeClock clock;
    const auto mono_before = clock.monotonic_now();
    const auto wall_before = clock.wall_now();

    clock.advance(std::chrono::milliseconds(250));

    EXPECT_EQ(clock.monotonic_now() - mono_before, std::chrono::milliseconds(250));
    EXPECT_EQ(std::chrono::duration_cast<std::chrono::milliseconds>(clock.wall_now() - wall_before),
              std::chrono::milliseconds(250));
}

TEST(FakeClock, AdvanceIsCumulative) {
    FakeClock clock;
    clock.advance(std::chrono::seconds(1));
    clock.advance(std::chrono::seconds(2));
    EXPECT_EQ(clock.monotonic_now().time_since_epoch(), std::chrono::seconds(3));
}

TEST(SystemClock, MonotonicNeverGoesBackwards) {
    SystemClock clock;
    const auto first = clock.monotonic_now();
    const auto second = clock.monotonic_now();
    EXPECT_LE(first, second);
}

}  // namespace
}  // namespace ssim::core
