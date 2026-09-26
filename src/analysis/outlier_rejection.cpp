#include "ssim/analysis/outlier_rejection.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace ssim::analysis {

namespace {

// Median of an already sorted, non-empty window. Same order statistics the
// earlier nth_element version picked, so the value is identical.
double sorted_median(const std::vector<double>& sorted) {
    const std::size_t mid = sorted.size() / 2;
    if (sorted.size() % 2 == 0) {
        return 0.5 * (sorted[mid] + sorted[mid - 1]);
    }
    return sorted[mid];
}

// Median of |z - med| over a sorted window without building or sorting the
// deviations. Values left of the median position have deviation med - z,
// values right of it z - med; each side is already in order, so the deviations
// come out sorted by merging the two sides (two pointers) and only the middle
// one or two are needed. The subtractions are the same ones abs(z - med) does,
// so the result is bit-identical.
double median_abs_deviation(const std::vector<double>& sorted, double med) {
    const std::size_t size = sorted.size();
    const std::size_t mid = size / 2;
    const std::size_t split = static_cast<std::size_t>(
        std::lower_bound(sorted.begin(), sorted.end(), med) - sorted.begin());

    std::size_t left = split;   // next left candidate is sorted[left - 1]
    std::size_t right = split;  // next right candidate is sorted[right]
    double previous = 0.0;
    double current = 0.0;
    for (std::size_t taken = 0; taken <= mid; ++taken) {
        const bool have_left = left > 0;
        const bool have_right = right < size;
        const double dev_left = have_left ? med - sorted[left - 1] : 0.0;
        const double dev_right = have_right ? sorted[right] - med : 0.0;
        previous = current;
        if (have_left && (!have_right || dev_left <= dev_right)) {
            current = dev_left;
            --left;
        } else {
            current = dev_right;
            ++right;
        }
    }
    return size % 2 == 0 ? 0.5 * (current + previous) : current;
}

}  // namespace

// The window slides along the kept samples: at each step at most one value
// enters and one leaves a sorted buffer, instead of copying and partially
// sorting up to 2*radius+1 values per sample. Flags set here never feed
// back into other windows (they use the raw heights), so the result matches
// the straightforward version exactly (see outlier_equivalence_test.cpp).
FilterResult apply_outlier_rejection(const FilterResult& input, double mad_k,
                                     int window_radius_samples) {
    FilterResult result = input;

    std::vector<std::size_t> kept_indices;
    kept_indices.reserve(result.samples.size());
    std::vector<double> kept_z;
    kept_z.reserve(result.samples.size());
    for (std::size_t i = 0; i < result.samples.size(); ++i) {
        if (result.samples[i].flag == SampleFlag::kKept) {
            kept_indices.push_back(i);
            kept_z.push_back(result.samples[i].z_m);
        }
    }
    const std::size_t n = kept_indices.size();
    if (n < 2) {
        return result;
    }
    const std::size_t radius = static_cast<std::size_t>(std::max(1, window_radius_samples));

    std::vector<double> window;
    window.reserve(2 * radius + 2);
    std::size_t window_lo = 0;
    std::size_t window_end = 0;  // one past the last index in the window

    for (std::size_t k = 0; k < n; ++k) {
        const std::size_t lo = (k > radius) ? (k - radius) : 0;
        const std::size_t end = std::min(n, k + radius + 1);

        for (; window_lo < lo; ++window_lo) {
            const auto leaving = std::lower_bound(window.begin(), window.end(), kept_z[window_lo]);
            window.erase(leaving);
        }
        for (; window_end < end; ++window_end) {
            const double entering = kept_z[window_end];
            window.insert(std::upper_bound(window.begin(), window.end(), entering), entering);
        }

        const double med = sorted_median(window);
        const double mad = median_abs_deviation(window, med);

        // PRD §8.4 step 3, literal threshold: "exceeds k times the median
        // absolute deviation" — no distribution-normalizing scale factor.
        const double threshold = mad_k * mad;
        if (threshold > 0.0) {
            if (std::abs(kept_z[k] - med) > threshold) {
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
