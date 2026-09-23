#pragma once

// FR-PRC-6 / PRD §8.4 step 8: propagate each line's fit standard error to a
// one-sigma stress uncertainty. Lines are independent measurements
// averaged into k_mean, so SE(k_mean) = sqrt(sum(SE(k_i)^2)) / N; that
// propagates through Stoney's (linear in k_mean) the same way the mean
// curvature itself does.

#include <vector>

#include "ssim/analysis/types.hpp"

namespace ssim::analysis {

double stress_uncertainty_pa(const std::vector<LineFitResult>& fits, double biaxial_modulus_pa,
                             double substrate_thickness_m, double film_thickness_m);

}  // namespace ssim::analysis
