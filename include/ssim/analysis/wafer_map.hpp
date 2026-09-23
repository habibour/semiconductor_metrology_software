#pragma once

// FR-PRC-8 / PRD §8.4 step 10: build a 2D height map by linear
// interpolation between adjacent scan lines in polar coordinates. Each
// scan line at angle theta covers the full diameter (s from -R to +R), so
// it contributes height data at both theta and theta+pi (s<0 on the theta
// line is physically the same ray as s>0 on the theta+pi line).

#include <cstddef>
#include <vector>

#include "ssim/analysis/types.hpp"

namespace ssim::analysis {

struct WaferMap {
    int width = 0;
    int height = 0;
    double grid_mm = 0.0;
    double diameter_m = 0.0;
    // Row-major, width*height entries in metres; NaN outside the wafer.
    std::vector<double> heights_m;
};

WaferMap build_wafer_map(const std::vector<LineFitResult>& fits, double diameter_m,
                         double grid_mm);

}  // namespace ssim::analysis
