#pragma once

// FR-PRC-5 / PRD §8.2 inverse model: subtract the pre-coating curvature k0
// and apply Stoney's equation. Units are SI throughout (pascals, metres,
// 1/metre); callers convert to MPa/um only at their own boundary.
//
// sigma = M_s * t_s^2 * (k_mean - k0) / (6 * t_f)

namespace ssim::analysis {

double stoney_stress_pa(double k_mean_per_m, double k0_per_m, double biaxial_modulus_pa,
                        double substrate_thickness_m, double film_thickness_m);

}  // namespace ssim::analysis
