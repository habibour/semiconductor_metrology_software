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

FilterResult apply_outlier_rejection(const FilterResult& input, double mad_k,
                                     int window_radius_samples) {
    FilterResult result = input;

    std::vector<std::size_t> kept_indices;
    for (std::size_t i = 0; i < result.samples.size(); ++i) {
        if (result.samples[i].flag == SampleFlag::kKept) {
            kept_indices.push_back(i);
        }
    }
    const std::size_t n = kept_indices.size();
    if (n < 2) {
        return result;
    }
    const std::size_t radius = static_cast<std::size_t>(std::max(1, window_radius_samples));

    for (std::size_t k = 0; k < n; ++k) {
        const std::size_t lo = (k > radius) ? (k - radius) : 0;
        const std::size_t hi = std::min(n - 1, k + radius);

        std::vector<double> window_z;
        window_z.reserve(hi - lo + 1);
        for (std::size_t w = lo; w <= hi; ++w) {
            window_z.push_back(result.samples[kept_indices[w]].z_m);
        }
        const double med = median(window_z);
        std::vector<double> abs_dev;
        abs_dev.reserve(window_z.size());
        for (double z : window_z) {
            abs_dev.push_back(std::abs(z - med));
        }
        const double mad = median(abs_dev);

        // PRD §8.4 step 3, literal threshold: "exceeds k times the median
        // absolute deviation" — no distribution-normalizing scale factor.
        const double threshold = mad_k * mad;
        if (threshold > 0.0) {
            const double zi = result.samples[kept_indices[k]].z_m;
            if (std::abs(zi - med) > threshold) {
                result.samples[kept_indices[k]].flag = SampleFlag::kOutlier;
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
