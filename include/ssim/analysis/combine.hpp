#pragma once

// FR-PRC-4 / PRD §8.4 step 7: average per-line curvatures into a mean, and
// report the per-angle spread as anisotropy = (max - min) / mean.
//
// Covers UT-ANISO-1: with N>=2 evenly-spaced lines over 180 degrees, the
// average of k(theta) equals (kx+ky)/2 regardless of anisotropy (PRD §8.2);
// with N=1 the bias is real and documented, not hidden.

#include <vector>

#include "ssim/analysis/types.hpp"

namespace ssim::analysis {

struct CombinedResult {
    double mean_curvature_per_m = 0.0;
    double anisotropy = 0.0;  // (max - min) / mean over per-line curvature
    std::size_t n_lines = 0;
};

CombinedResult combine_lines(const std::vector<LineFitResult>& fits);

}  // namespace ssim::analysis
