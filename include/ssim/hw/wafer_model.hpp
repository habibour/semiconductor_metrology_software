#pragma once

// Thread-safety: WaferModel is immutable after construction — safe to share
// read-only across threads (the scan thread and, later, the processing
// pool both read from the same WaferModel for a given wafer).
//
// FR-HW-2 / PRD §8.2: builds the hidden ground truth for one wafer directly
// from configuration (sigma, k0, tilt, anisotropy are literal config
// values, not sampled) and answers the forward model, z(s), before noise or
// faults. The config's seed is stored and handed to callers that need their
// own seeded RNG (the laser sensor's noise, the fault injector) — the
// wafer's *shape* is deterministic from config alone; only the *noise and
// faults* layered on top by the laser sensor are random, and are made
// reproducible by that shared seed (PRD §8.3, "one seeded generator per
// wafer; the seed is written into the output").

#include <cstdint>

#include "ssim/core/config.hpp"

namespace ssim::hw {

struct WaferTruth {
    double stress_pa;                // sigma
    double initial_curvature_per_m;  // k0
    double tilt_x;                   // p: dz/dx, dimensionless (m/m)
    double tilt_y;                   // q: dz/dy, dimensionless (m/m)
    double anisotropy;               // alpha
    double film_curvature_per_m;     // 6*sigma*t_f / (M_s*t_s^2), film alone
    double total_curvature_per_m;    // k = k0 + film_curvature_per_m
    double kx_per_m;                 // total_curvature * (1 - alpha/2)
    double ky_per_m;                 // total_curvature * (1 + alpha/2)
    double diameter_m;
};

class WaferModel {
public:
    explicit WaferModel(const ssim::core::WaferConfig& config);

    const WaferTruth& truth() const { return truth_; }
    std::uint64_t seed() const { return seed_; }

    // Curvature at scan angle theta (radians), per PRD §8.2:
    // k(theta) = kx*cos(theta)^2 + ky*sin(theta)^2, where kx/ky derive from
    // the *total* curvature k = k0 + film (PRD §8.2's literal formula — the
    // anisotropy split is applied to k, not to the film term alone).
    double curvature_at_m(double theta_rad) const;

    // Ideal height (metres) at position s (metres, signed) along the line
    // at angle theta_rad, before noise or faults (PRD §8.2 forward model).
    double height_m(double theta_rad, double s_m) const;

private:
    WaferTruth truth_;
    std::uint64_t seed_;
};

}  // namespace ssim::hw
