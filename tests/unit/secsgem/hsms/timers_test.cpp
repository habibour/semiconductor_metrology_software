#include "ssim/secsgem/hsms/timers.hpp"

#include <gtest/gtest.h>

#include <chrono>

#include "ssim/core/clock.hpp"

namespace ssim::secsgem::hsms {
namespace {

using std::chrono::milliseconds;

TEST(Timer, NotRunningNeverExpires) {
    ssim::core::FakeClock clock;
    Timer t;
    EXPECT_FALSE(t.running());
    clock.advance(milliseconds(1000000));
    EXPECT_FALSE(t.expired(clock.monotonic_now()));
}

TEST(Timer, ExpiresExactlyAtItsDeadline) {
    ssim::core::FakeClock clock;
    Timer t;
    t.start(clock.monotonic_now(), Duration(1000));
    EXPECT_TRUE(t.running());
    clock.advance(milliseconds(999));
    EXPECT_FALSE(t.expired(clock.monotonic_now()));
    clock.advance(milliseconds(1));
    EXPECT_TRUE(t.expired(clock.monotonic_now()));
}

TEST(Timer, StaysExpiredUntilStoppedSoAMissedTickLosesNothing) {
    ssim::core::FakeClock clock;
    Timer t;
    t.start(clock.monotonic_now(), Duration(10));
    clock.advance(milliseconds(500));
    EXPECT_TRUE(t.expired(clock.monotonic_now()));
    EXPECT_TRUE(t.expired(clock.monotonic_now()));
    t.stop();
    EXPECT_FALSE(t.expired(clock.monotonic_now()));
    EXPECT_FALSE(t.running());
}

TEST(Timer, RestartingMovesTheDeadline) {
    ssim::core::FakeClock clock;
    Timer t;
    t.start(clock.monotonic_now(), Duration(1000));
    clock.advance(milliseconds(900));
    t.start(clock.monotonic_now(), Duration(1000));  // restart
    clock.advance(milliseconds(900));
    EXPECT_FALSE(t.expired(clock.monotonic_now()));
    clock.advance(milliseconds(100));
    EXPECT_TRUE(t.expired(clock.monotonic_now()));
}

}  // namespace
}  // namespace ssim::secsgem::hsms
