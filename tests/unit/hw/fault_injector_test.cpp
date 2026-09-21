#include "ssim/hw/fault_injector.hpp"

#include <gtest/gtest.h>

#include <cmath>

namespace ssim::hw {
namespace {

constexpr double kCleanHeightM = 0.0001;  // 100 um, an arbitrary non-zero clean value
constexpr double kUmToM = 1e-6;

TEST(FaultInjector, SpikeAlwaysOffsetsByAmplitudeWhenRateIsOne) {
    FaultSpec spec;
    spec.type = FaultType::kSpike;
    spec.rate = 1.0;
    spec.amplitude_um = 40.0;
    FaultInjector injector(spec, /*seed=*/1);

    for (std::size_t i = 0; i < 10; ++i) {
        auto sample = injector.apply(i, 0.0, kCleanHeightM);
        ASSERT_TRUE(sample.height_m.has_value());
        EXPECT_NEAR(std::abs(*sample.height_m - kCleanHeightM), 40.0 * kUmToM, 1e-12);
    }
}

TEST(FaultInjector, SpikeNeverTriggersWhenRateIsZero) {
    FaultSpec spec;
    spec.type = FaultType::kSpike;
    spec.rate = 0.0;
    spec.amplitude_um = 40.0;
    FaultInjector injector(spec, /*seed=*/1);

    for (std::size_t i = 0; i < 10; ++i) {
        auto sample = injector.apply(i, 0.0, kCleanHeightM);
        EXPECT_DOUBLE_EQ(*sample.height_m, kCleanHeightM);
    }
}

TEST(FaultInjector, BurstOffsetsARunOfConsecutiveSamples) {
    FaultSpec spec;
    spec.type = FaultType::kBurst;
    spec.rate = 1.0;  // triggers immediately
    spec.amplitude_um = 40.0;
    spec.length_samples = 3;
    FaultInjector injector(spec, /*seed=*/2);

    for (std::size_t i = 0; i < 3; ++i) {
        auto sample = injector.apply(i, 0.0, kCleanHeightM);
        EXPECT_NEAR(std::abs(*sample.height_m - kCleanHeightM), 40.0 * kUmToM, 1e-12);
    }
}

TEST(FaultInjector, DropoutProducesAGapOfMissingSamples) {
    FaultSpec spec;
    spec.type = FaultType::kDropout;
    spec.rate = 1.0;
    spec.length_samples = 2;
    FaultInjector injector(spec, /*seed=*/3);

    EXPECT_FALSE(injector.apply(0, 0.0, kCleanHeightM).height_m.has_value());
    EXPECT_FALSE(injector.apply(1, 0.0, kCleanHeightM).height_m.has_value());
}

TEST(FaultInjector, StuckRepeatsTheValueAtTriggerTimeNotLaterCleanValues) {
    FaultSpec spec;
    spec.type = FaultType::kStuck;
    spec.rate = 1.0;
    spec.length_samples = 2;
    FaultInjector injector(spec, /*seed=*/4);

    auto first = injector.apply(0, 0.0, 0.001);
    auto second = injector.apply(1, 0.0, 0.002);  // different clean value

    ASSERT_TRUE(first.height_m.has_value());
    ASSERT_TRUE(second.height_m.has_value());
    EXPECT_DOUBLE_EQ(*first.height_m, 0.001);
    EXPECT_DOUBLE_EQ(*second.height_m, 0.001);  // still the value from trigger time
}

TEST(FaultInjector, DriftAddsALinearOffsetOverElapsedTime) {
    FaultSpec spec;
    spec.type = FaultType::kDrift;
    spec.drift_um_per_s = 2.0;
    FaultInjector injector(spec, /*seed=*/5);

    auto at_zero_s = injector.apply(0, 0.0, kCleanHeightM);
    auto at_three_s = injector.apply(1, 3.0, kCleanHeightM);

    EXPECT_DOUBLE_EQ(*at_zero_s.height_m, kCleanHeightM);
    EXPECT_NEAR(*at_three_s.height_m, kCleanHeightM + 6.0 * kUmToM, 1e-12);  // 2 um/s * 3s
}

TEST(FaultInjector, SaturationClipsValuesBeyondTheLimit) {
    FaultSpec spec;
    spec.type = FaultType::kSaturation;
    spec.saturation_limit_um = 10.0;
    FaultInjector injector(spec, /*seed=*/6);

    auto within_range = injector.apply(0, 0.0, 5.0 * kUmToM);
    auto beyond_positive = injector.apply(1, 0.0, 50.0 * kUmToM);
    auto beyond_negative = injector.apply(2, 0.0, -50.0 * kUmToM);

    EXPECT_DOUBLE_EQ(*within_range.height_m, 5.0 * kUmToM);
    EXPECT_DOUBLE_EQ(*beyond_positive.height_m, 10.0 * kUmToM);
    EXPECT_DOUBLE_EQ(*beyond_negative.height_m, -10.0 * kUmToM);
}

TEST(FaultInjector, StageStallFlagsWhenTriggeredAndNeverWhenRateIsZero) {
    FaultSpec always_spec;
    always_spec.type = FaultType::kStageStall;
    always_spec.rate = 1.0;
    always_spec.stall_duration_s = 2.0;
    FaultInjector always(always_spec, /*seed=*/7);
    EXPECT_TRUE(always.apply(0, 0.0, kCleanHeightM).stage_stall);

    FaultSpec never_spec;
    never_spec.type = FaultType::kStageStall;
    never_spec.rate = 0.0;
    FaultInjector never(never_spec, /*seed=*/8);
    EXPECT_FALSE(never.apply(0, 0.0, kCleanHeightM).stage_stall);
}

}  // namespace
}  // namespace ssim::hw
