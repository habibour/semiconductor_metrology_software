#include "ssim/analysis/combine.hpp"

#include <algorithm>
#include <cmath>

namespace ssim::analysis {

CombinedResult combine_lines(const std::vector<LineFitResult>& fits) {
    CombinedResult result;
    double sum = 0.0;
    double lo = 0.0;
    double hi = 0.0;
    bool first = true;
    for (const auto& fit : fits) {
        if (!fit.valid) {
            continue;
        }
        sum += fit.curvature_per_m;
        lo = first ? fit.curvature_per_m : std::min(lo, fit.curvature_per_m);
        hi = first ? fit.curvature_per_m : std::max(hi, fit.curvature_per_m);
        first = false;
        ++result.n_lines;
    }
    if (result.n_lines == 0) {
        return result;
    }
    result.mean_curvature_per_m = sum / static_cast<double>(result.n_lines);
    if (result.mean_curvature_per_m != 0.0) {
        result.anisotropy = (hi - lo) / result.mean_curvature_per_m;
    }
    return result;
}

}  // namespace ssim::analysis
