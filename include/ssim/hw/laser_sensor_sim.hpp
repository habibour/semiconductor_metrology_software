#pragma once

// Thread-safety: Owned by the scan thread. Not safe to call from more than
// one thread concurrently. Holds a non-owning reference to the IStage it
// reads position from and to the WaferModel it reads shape from — the
// caller (the hardware factory) must keep both alive at least as long as
// this object.

#include <cstdint>
#include <random>

#include "ssim/hw/ilaser_sensor.hpp"
#include "ssim/hw/istage.hpp"
#include "ssim/hw/wafer_model.hpp"

namespace ssim::hw {

class LaserSensorSim final : public ILaserSensor {
public:
    // sigma_m: PRD §8.3 Gaussian noise standard deviation, in metres.
    // seed: PRD §8.3 "one seeded generator per wafer" — pass WaferModel::seed().
    LaserSensorSim(const IStage& stage, const WaferModel& wafer_model, double sigma_m,
                   std::uint64_t seed);

    double read_height_m() override;

private:
    const IStage& stage_;
    const WaferModel& wafer_model_;
    std::mt19937_64 rng_;
    // std::normal_distribution requires sigma > 0 (libstdc++ asserts on it), so
    // zero noise is handled here instead of by passing 0 to the distribution.
    bool noisy_;
    std::normal_distribution<double> noise_;
};

}  // namespace ssim::hw
