#pragma once

// FR-PRC-3 / PRD §8.4 step 5: per-line least-squares fit of
// z = a*s^2 + b*s + c over the samples still kKept after edge exclusion and
// outlier rejection. curvature_per_m = 2*a; se_curvature_per_m = 2 * the
// standard error of a (propagated from the fit's residual variance).
//
// Covers UT-FIT-1 (noise-free: exact recovery to numeric precision) and
// UT-FIT-2 (noisy: within a statistical bound, reported SE matches repeated
// trials).

#include "ssim/analysis/types.hpp"

namespace ssim::analysis {

LineFitResult fit_line(const FilterResult& filtered);

}  // namespace ssim::analysis
