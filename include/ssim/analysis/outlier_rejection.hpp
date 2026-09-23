#pragma once

// FR-PRC-2 / PRD §8.4 step 3: robust outlier rejection by median and
// median absolute deviation (MAD), applied only to samples still kKept
// after edge exclusion — edge-excluded and dropped samples pass through
// unchanged so removed_count/removed_fraction stay cumulative.

#include "ssim/analysis/types.hpp"

namespace ssim::analysis {

FilterResult apply_outlier_rejection(const FilterResult& input, double mad_k);

}  // namespace ssim::analysis
