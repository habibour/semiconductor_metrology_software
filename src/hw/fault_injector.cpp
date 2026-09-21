#include "ssim/hw/fault_injector.hpp"

#include <algorithm>
#include <cmath>

namespace ssim::hw {

namespace {
constexpr double kUmToM = 1e-6;
}  // namespace

FaultInjector::FaultInjector(FaultSpec spec, std::uint64_t seed)
    : spec_(spec), rng_(seed), trigger_(spec.rate) {}

FaultSample FaultInjector::apply(std::size_t /*sample_index*/, double elapsed_s,
                                 double clean_height_m) {
    switch (spec_.type) {
        case FaultType::kSpike: {
            if (trigger_(rng_)) {
                const double sign = std::bernoulli_distribution(0.5)(rng_) ? 1.0 : -1.0;
                return FaultSample{clean_height_m + sign * spec_.amplitude_um * kUmToM, false};
            }
            return FaultSample{clean_height_m, false};
        }
        case FaultType::kBurst: {
            if (run_remaining_ == 0 && trigger_(rng_)) {
                run_remaining_ = static_cast<std::size_t>(std::max(1, spec_.length_samples));
            }
            if (run_remaining_ > 0) {
                --run_remaining_;
                const double sign = std::bernoulli_distribution(0.5)(rng_) ? 1.0 : -1.0;
                return FaultSample{clean_height_m + sign * spec_.amplitude_um * kUmToM, false};
            }
            return FaultSample{clean_height_m, false};
        }
        case FaultType::kDropout: {
            if (run_remaining_ == 0 && trigger_(rng_)) {
                run_remaining_ = static_cast<std::size_t>(std::max(1, spec_.length_samples));
            }
            if (run_remaining_ > 0) {
                --run_remaining_;
                return FaultSample{std::nullopt, false};
            }
            return FaultSample{clean_height_m, false};
        }
        case FaultType::kStuck: {
            if (run_remaining_ == 0 && trigger_(rng_)) {
                run_remaining_ = static_cast<std::size_t>(std::max(1, spec_.length_samples));
                stuck_value_m_ = clean_height_m;
            }
            if (run_remaining_ > 0) {
                --run_remaining_;
                return FaultSample{stuck_value_m_, false};
            }
            return FaultSample{clean_height_m, false};
        }
        case FaultType::kDrift: {
            return FaultSample{clean_height_m + spec_.drift_um_per_s * kUmToM * elapsed_s, false};
        }
        case FaultType::kSaturation: {
            const double limit_m = spec_.saturation_limit_um * kUmToM;
            return FaultSample{std::clamp(clean_height_m, -limit_m, limit_m), false};
        }
        case FaultType::kStageStall: {
            return FaultSample{clean_height_m, trigger_(rng_)};
        }
    }
    return FaultSample{clean_height_m, false};
}

}  // namespace ssim::hw
