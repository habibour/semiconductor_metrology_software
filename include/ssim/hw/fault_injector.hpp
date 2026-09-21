#pragma once

// Thread-safety: Not thread-safe (owns mutable run-length state and an
// RNG); intended for exclusive use by the scan thread that owns the sensor
// it is applied to.
//
// FR-HW-4 / PRD §8.3: the seven fault primitives. This builds the
// mechanism only — apply() is a pure per-sample transform with no
// knowledge of alarms or the event bus; wiring fault occurrences into
// alarms (SensorSpikeRateHigh, SensorDropout, ...) happens in the
// processing pipeline, which is Day 2 scope.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <random>

namespace ssim::hw {

enum class FaultType {
    kSpike,
    kBurst,
    kDropout,
    kStuck,
    kDrift,
    kSaturation,
    kStageStall,
};

struct FaultSpec {
    FaultType type;
    double rate = 0.0;  // spike/burst/dropout/stuck/stage-stall: trigger probability per sample
    double amplitude_um = 0.0;         // spike/burst: offset magnitude
    int length_samples = 1;            // burst/dropout/stuck: run length once triggered
    double drift_um_per_s = 0.0;       // drift: linear rate
    double saturation_limit_um = 0.0;  // saturation: clip magnitude
    double stall_duration_s = 0.0;     // stage stall: pause length once triggered
};

struct FaultSample {
    std::optional<double> height_m;  // nullopt => sample dropped (dropout)
    bool stage_stall = false;        // true => caller should pause the stage
};

class FaultInjector {
public:
    FaultInjector(FaultSpec spec, std::uint64_t seed);

    // Applies the configured fault to one clean sample. sample_index counts
    // samples since construction; elapsed_s is simulated time since the
    // same point (used by Drift).
    FaultSample apply(std::size_t sample_index, double elapsed_s, double clean_height_m);

private:
    FaultSpec spec_;
    std::mt19937_64 rng_;
    std::bernoulli_distribution trigger_;
    std::size_t run_remaining_ = 0;
    double stuck_value_m_ = 0.0;
};

}  // namespace ssim::hw
