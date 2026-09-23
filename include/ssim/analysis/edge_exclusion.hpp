#pragma once

// FR-PRC-1 / PRD §8.4 step 2: drop samples within edge_exclusion_mm of
// either end of the line. "End" means the physical extremes of this line's
// own samples (min/max s_m), not a fixed radius, so it works the same for
// every configured wafer diameter without the caller doing unit math.

#include "ssim/analysis/types.hpp"

namespace ssim::analysis {

FilterResult apply_edge_exclusion(const LineSamples& line, double edge_exclusion_mm);

}  // namespace ssim::analysis
