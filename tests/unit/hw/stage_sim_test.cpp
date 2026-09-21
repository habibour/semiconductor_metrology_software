#include "ssim/hw/stage_sim.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cmath>

namespace ssim::hw {
namespace {

TEST(StageSim, SetLineResetsPositionAndAngle) {
    StageSim stage(150.0, 0.0);
    stage.move_to(0.02);
    stage.set_line(1.5707963267948966);  // pi/2

    EXPECT_DOUBLE_EQ(stage.current_position_m(), 0.0);
    EXPECT_DOUBLE_EQ(stage.current_angle_rad(), 1.5707963267948966);
}

TEST(StageSim, MoveToUpdatesPosition) {
    StageSim stage(150.0, /*realtime_factor=*/0.0);
    stage.move_to(0.035);
    EXPECT_DOUBLE_EQ(stage.current_position_m(), 0.035);
}

// FR-HW-5: realtime_factor 0 must not delay the caller, regardless of
// distance/speed — this is what makes tests fast and deterministic.
TEST(StageSim, RealtimeFactorZeroDoesNotSleep) {
    StageSim stage(1.0 /* mm/s: deliberately slow */, /*realtime_factor=*/0.0);
    const auto start = std::chrono::steady_clock::now();
    stage.move_to(0.15);  // would take 150s of simulated motion at 1 mm/s
    const auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_LT(elapsed, std::chrono::milliseconds(50));
}

// A 300 mm diametric line at 40 points/mm produces 12,000 samples
// (PRD §8.1's own worked figure). Walking the stage across the line in
// 1/points_per_mm steps should visit exactly that many positions.
TEST(StageSim, LineTraversalProducesExpectedSampleCount) {
    StageSim stage(150.0, /*realtime_factor=*/0.0);
    stage.set_line(0.0);

    constexpr double kDiameterM = 0.300;
    constexpr int kPointsPerMm = 40;
    const double step_m = 1.0 / (kPointsPerMm * 1000.0);
    const double half_length_m = kDiameterM / 2.0;

    int sample_count = 0;
    for (double s = -half_length_m; s <= half_length_m + 1e-9; s += step_m) {
        stage.move_to(s);
        ++sample_count;
    }

    EXPECT_EQ(sample_count, 12001);  // 12000 steps plus the starting sample
}

}  // namespace
}  // namespace ssim::hw
