#include "ssim/hw/laser_sensor_sim.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <numeric>
#include <vector>

#include "ssim/hw/stage_sim.hpp"
#include "ssim/hw/wafer_model.hpp"

namespace ssim::hw {
namespace {

ssim::core::WaferConfig flat_wafer_config() {
    ssim::core::WaferConfig cfg;
    cfg.diameter_mm = 300.0;
    cfg.thickness_um = 775.0;
    cfg.film_thickness_um = 1.0;
    cfg.biaxial_modulus_gpa = 180.5;
    // Zero everything that would add shape, so the ideal height at the
    // stage's fixed position is exactly 0 and any spread in readings is
    // purely the injected noise.
    cfg.truth.stress_mpa = 0.0;
    cfg.truth.initial_curvature_1_per_m = 0.0;
    cfg.truth.tilt_x_um_per_mm = 0.0;
    cfg.truth.tilt_y_um_per_mm = 0.0;
    cfg.truth.anisotropy = 0.0;
    cfg.seed = 2024;
    return cfg;
}

TEST(LaserSensorSim, ReadsIdealHeightWhenNoiseIsZero) {
    WaferModel model(flat_wafer_config());
    StageSim stage(150.0, 0.0);
    stage.set_line(0.0);
    stage.move_to(0.05);

    LaserSensorSim laser(stage, model, /*sigma_m=*/0.0, model.seed());
    EXPECT_DOUBLE_EQ(laser.read_height_m(), model.height_m(0.0, 0.05));
}

TEST(LaserSensorSim, SameSeedGivesIdenticalNoiseSequence) {
    WaferModel model(flat_wafer_config());
    StageSim stage_a(150.0, 0.0);
    StageSim stage_b(150.0, 0.0);
    LaserSensorSim laser_a(stage_a, model, /*sigma_m=*/1e-6, model.seed());
    LaserSensorSim laser_b(stage_b, model, /*sigma_m=*/1e-6, model.seed());

    for (int i = 0; i < 20; ++i) {
        EXPECT_DOUBLE_EQ(laser_a.read_height_m(), laser_b.read_height_m());
    }
}

// Noise statistics are sane at rtf=0 (no real time elapses; this is purely
// about the RNG's output distribution, per day-1-plan.md's test bullet for
// this file).
TEST(LaserSensorSim, NoiseStatisticsMatchConfiguredSigma) {
    WaferModel model(flat_wafer_config());
    StageSim stage(150.0, /*realtime_factor=*/0.0);
    stage.set_line(0.0);
    stage.move_to(0.0);  // ideal height is 0 here (flat wafer, centre)

    constexpr double kSigmaM = 0.5e-6;  // PRD §8.3 default: 0.5 um
    LaserSensorSim laser(stage, model, kSigmaM, model.seed());

    constexpr int kSamples = 20000;
    std::vector<double> readings;
    readings.reserve(kSamples);
    for (int i = 0; i < kSamples; ++i) {
        readings.push_back(laser.read_height_m());
    }

    const double mean = std::accumulate(readings.begin(), readings.end(), 0.0) / kSamples;
    double variance = 0.0;
    for (double r : readings) {
        variance += (r - mean) * (r - mean);
    }
    variance /= kSamples;
    const double sample_sigma = std::sqrt(variance);

    // Loose statistical tolerances: this checks the noise model is roughly
    // right, not a precise numeric-precision comparison.
    EXPECT_NEAR(mean, 0.0, kSigmaM * 0.1);
    EXPECT_NEAR(sample_sigma, kSigmaM, kSigmaM * 0.1);
}

}  // namespace
}  // namespace ssim::hw
