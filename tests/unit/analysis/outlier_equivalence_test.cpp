// The outlier filter was rewritten for speed (docs/benchmarks.md). It must give
// exactly the same result as the straightforward version it replaced, so that
// version is kept here as the reference and the two are compared on many random
// lines: every flag, the removed count and the removed fraction.

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

#include "ssim/analysis/outlier_rejection.hpp"

namespace ssim::analysis {
namespace {

// ---- the reference: the original implementation, unchanged -----------------

double reference_median(std::vector<double> values) {
    if (values.empty()) {
        return 0.0;
    }
    const std::size_t mid = values.size() / 2;
    std::nth_element(values.begin(), values.begin() + static_cast<long>(mid), values.end());
    double m = values[mid];
    if (values.size() % 2 == 0) {
        std::nth_element(values.begin(), values.begin() + static_cast<long>(mid) - 1, values.end());
        m = 0.5 * (m + values[mid - 1]);
    }
    return m;
}

FilterResult reference_outlier_rejection(const FilterResult& input, double mad_k,
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
        for (std::size_t w = lo; w <= hi; ++w) {
            window_z.push_back(result.samples[kept_indices[w]].z_m);
        }
        const double med = reference_median(window_z);
        std::vector<double> abs_dev;
        for (double z : window_z) {
            abs_dev.push_back(std::abs(z - med));
        }
        const double mad = reference_median(abs_dev);
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

// ---- random lines -----------------------------------------------------------

enum class Shape { kNoisy, kParabola, kConstant, kFewLevels, kHeavySpikes };

FilterResult make_line(std::mt19937_64& rng, std::size_t n, Shape shape, double excluded_fraction) {
    std::normal_distribution<double> noise(0.0, 0.5e-6);
    std::uniform_real_distribution<double> unit(0.0, 1.0);
    std::uniform_int_distribution<int> level(0, 3);
    FilterResult line;
    for (std::size_t i = 0; i < n; ++i) {
        const double s = -0.15 + 0.3 * static_cast<double>(i) /
                                     static_cast<double>(std::max<std::size_t>(1, n - 1));
        double z = 0.0;
        switch (shape) {
            case Shape::kNoisy:
                z = noise(rng);
                break;
            case Shape::kParabola:
                z = -0.005 * s * s + 0.001 * s + noise(rng);
                break;
            case Shape::kConstant:
                z = 1.25e-6;
                break;
            case Shape::kFewLevels:
                z = level(rng) * 0.25e-6;
                break;  // many exact ties
            case Shape::kHeavySpikes:
                z = noise(rng) + (unit(rng) < 0.2 ? 40e-6 * (unit(rng) < 0.5 ? 1 : -1) : 0.0);
                break;
        }
        if (shape != Shape::kHeavySpikes && unit(rng) < 0.01) {
            z += 40e-6;  // an occasional spike
        }
        SampleFlag flag = SampleFlag::kKept;
        if (unit(rng) < excluded_fraction) {
            flag = unit(rng) < 0.5 ? SampleFlag::kEdgeExcluded : SampleFlag::kDropped;
        }
        line.samples.push_back(FilteredSample{s, z, flag});
    }
    for (const auto& sample : line.samples) {
        if (sample.flag != SampleFlag::kKept) {
            ++line.removed_count;
        }
    }
    line.removed_fraction =
        n > 0 ? static_cast<double>(line.removed_count) / static_cast<double>(n) : 0.0;
    return line;
}

void expect_same(const FilterResult& expected, const FilterResult& actual,
                 const std::string& what) {
    ASSERT_EQ(expected.samples.size(), actual.samples.size()) << what;
    for (std::size_t i = 0; i < expected.samples.size(); ++i) {
        ASSERT_EQ(static_cast<int>(expected.samples[i].flag),
                  static_cast<int>(actual.samples[i].flag))
            << what << ", sample " << i;
        ASSERT_EQ(expected.samples[i].z_m, actual.samples[i].z_m) << what;
        ASSERT_EQ(expected.samples[i].s_m, actual.samples[i].s_m) << what;
    }
    ASSERT_EQ(expected.removed_count, actual.removed_count) << what;
    ASSERT_EQ(expected.removed_fraction, actual.removed_fraction) << what;
}

TEST(OutlierEquivalence, MatchesTheReferenceOnThousandsOfRandomLines) {
    std::mt19937_64 rng(20260926);
    const Shape shapes[] = {Shape::kNoisy, Shape::kParabola, Shape::kConstant, Shape::kFewLevels,
                            Shape::kHeavySpikes};
    const int radii[] = {1, 2, 3, 7, 40, 41, 100};
    const double ks[] = {2.0, 3.0, 6.0, 10.0};
    const double excluded[] = {0.0, 0.05, 0.4};

    int cases = 0;
    for (int trial = 0; trial < 2500; ++trial) {
        // Lengths from empty to longer than any window, with many tiny ones.
        const std::size_t n = trial % 5 == 0 ? static_cast<std::size_t>(rng() % 12)
                                             : static_cast<std::size_t>(rng() % 400);
        const Shape shape = shapes[rng() % 5];
        const int radius = radii[rng() % 7];
        const double k = ks[rng() % 4];
        const double ex = excluded[rng() % 3];
        const FilterResult line = make_line(rng, n, shape, ex);

        const FilterResult expected = reference_outlier_rejection(line, k, radius);
        const FilterResult actual = apply_outlier_rejection(line, k, radius);
        expect_same(expected, actual,
                    "trial " + std::to_string(trial) + " n=" + std::to_string(n) +
                        " radius=" + std::to_string(radius) + " k=" + std::to_string(k));
        if (::testing::Test::HasFatalFailure()) {
            return;
        }
        ++cases;
    }
    EXPECT_EQ(cases, 2500);
}

TEST(OutlierEquivalence, MatchesOnFullSizedWafersWithTheDefaultWindow) {
    std::mt19937_64 rng(11);
    for (Shape shape : {Shape::kParabola, Shape::kHeavySpikes, Shape::kFewLevels}) {
        const FilterResult line = make_line(rng, 12001, shape, 0.001);
        expect_same(reference_outlier_rejection(line, 6.0, 40),
                    apply_outlier_rejection(line, 6.0, 40), "full-sized line");
    }
}

}  // namespace
}  // namespace ssim::analysis
