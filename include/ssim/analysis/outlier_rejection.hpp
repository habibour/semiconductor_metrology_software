#pragma once

// FR-PRC-2 / PRD §8.4 step 3: robust outlier rejection by median and
// median absolute deviation (MAD), applied only to samples still kKept
// after edge exclusion — edge-excluded and dropped samples pass through
// unchanged so removed_count/removed_fraction stay cumulative.
//
// PRD's literal wording is "residual from a *running* median" — a single
// global median over a whole line would treat the line's own parabolic
// shape as noise (samples near the wafer edge sit ~100um from the samples
// near the centre purely from curvature, dwarfing the ~0.5um sensor
// noise), so this uses a local median/MAD over a sliding window of nearby
// kept samples instead. The window is small enough that the wafer's own
// curvature is negligible across it (a few nanometres of sagitta over a
// 2mm window at typical curvatures, versus ~um-scale noise).

#include "ssim/analysis/types.hpp"

namespace ssim::analysis {

FilterResult apply_outlier_rejection(const FilterResult& input, double mad_k,
                                     int window_radius_samples = 40);

}  // namespace ssim::analysis
