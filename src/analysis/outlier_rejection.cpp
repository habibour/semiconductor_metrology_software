#include "ssim/analysis/outlier_rejection.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace ssim::analysis {

namespace {
double median(std::vector<double> values) {
    if (values.empty()) {
        return 0.0;
    }
    const std::size_t mid = values.size() / 2;
    std::nth_element(values.begin(), values.begin() + static_cast<long>(mid), values.end());
    double m = values[mid];
    if (values.size() % 2 == 0) {
        std::nth_element(values.begin(), values.begin() + static_cast<long>(mid) - 1,
                         values.end());
        m = 0.5 * (m + values[mid - 1]);
    }
    return m;
}
}  // namespace

FilterResult apply_outlier_rejection(const FilterResult& input, double mad_k) {
    FilterResult result = input;

    std::vector<double> kept_z;
    for (const auto& s : result.samples) {
        if (s.flag == SampleFlag::kKept) {
            kept_z.push_back(s.z_m);
        }
    }
    if (kept_z.size() < 2) {
        return result;  // not enough points for a meaningful median/MAD
    }

    const double med = median(kept_z);
    std::vector<double> abs_dev;
    abs_dev.reserve(kept_z.size());
    for (double z : kept_z) {
        abs_dev.push_back(std::abs(z - med));
    }
    const double mad = median(abs_dev);

    // PRD §8.4 step 3, literal threshold: "exceeds k times the median
    // absolute deviation" — no distribution-normalizing scale factor.
    const double threshold = mad_k * mad;
    if (threshold > 0.0) {
        for (auto& s : result.samples) {
            if (s.flag == SampleFlag::kKept && std::abs(s.z_m - med) > threshold) {
                s.flag = SampleFlag::kOutlier;
            }
        }
    }

    result.removed_count = static_cast<std::size_t>(
        std::count_if(result.samples.begin(), result.samples.end(),
                      [](const FilteredSample& s) { return s.flag != SampleFlag::kKept; }));
    result.removed_fraction =
        static_cast<double>(result.removed_count) / static_cast<double>(result.samples.size());
    return result;
}

}  // namespace ssim::analysis
