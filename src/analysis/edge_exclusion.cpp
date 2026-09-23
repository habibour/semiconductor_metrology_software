#include "ssim/analysis/edge_exclusion.hpp"

#include <algorithm>
#include <cmath>

namespace ssim::analysis {

FilterResult apply_edge_exclusion(const LineSamples& line, double edge_exclusion_mm) {
    FilterResult result;
    result.angle_rad = line.angle_rad;
    result.samples.reserve(line.s_m.size());

    if (line.s_m.empty()) {
        return result;
    }

    const double edge_exclusion_m = edge_exclusion_mm * 1e-3;
    const auto [min_it, max_it] = std::minmax_element(line.s_m.begin(), line.s_m.end());
    const double lo = *min_it + edge_exclusion_m;
    const double hi = *max_it - edge_exclusion_m;

    for (std::size_t i = 0; i < line.s_m.size(); ++i) {
        const double s = line.s_m[i];
        const double z = line.z_m[i];
        FilteredSample fs;
        fs.s_m = s;
        fs.z_m = z;
        if (std::isnan(z)) {
            fs.flag = SampleFlag::kDropped;
        } else if (s < lo || s > hi) {
            fs.flag = SampleFlag::kEdgeExcluded;
        } else {
            fs.flag = SampleFlag::kKept;
        }
        result.samples.push_back(fs);
    }

    result.removed_count = static_cast<std::size_t>(
        std::count_if(result.samples.begin(), result.samples.end(),
                      [](const FilteredSample& s) { return s.flag != SampleFlag::kKept; }));
    result.removed_fraction =
        static_cast<double>(result.removed_count) / static_cast<double>(result.samples.size());
    return result;
}

}  // namespace ssim::analysis
