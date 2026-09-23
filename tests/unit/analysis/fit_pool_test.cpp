#include "ssim/analysis/fit_pool.hpp"

#include <gtest/gtest.h>

#include <random>

#include "ssim/analysis/line_fit.hpp"

namespace ssim::analysis {
namespace {

FilterResult make_line(double a, double b, double c, std::uint64_t seed) {
    FilterResult result;
    std::mt19937_64 rng(seed);
    std::normal_distribution<double> noise(0.0, 0.5e-6);
    for (int i = 0; i < 41; ++i) {
        const double s = -0.15 + 0.3 * static_cast<double>(i) / 40.0;
        const double z = a * s * s + b * s + c + noise(rng);
        result.samples.push_back(FilteredSample{s, z, SampleFlag::kKept});
    }
    return result;
}

// FR-PRC-9: results from the pool must be bit-for-bit identical to fitting
// the same lines serially, regardless of how many worker threads ran them.
TEST(FitThreadPool, MatchesSerialFitBitForBit) {
    std::vector<FilterResult> lines;
    for (int i = 0; i < 8; ++i) {
        lines.push_back(make_line(-0.005 + 0.0001 * i, 0.001 * i, 1e-6 * i,
                                  /*seed=*/static_cast<std::uint64_t>(1000 + i)));
    }

    std::vector<LineFitResult> serial;
    for (const auto& line : lines) {
        serial.push_back(fit_line(line));
    }

    FitThreadPool pool(4);
    auto pooled = pool.fit_lines(lines);

    ASSERT_EQ(pooled.size(), serial.size());
    for (std::size_t i = 0; i < serial.size(); ++i) {
        EXPECT_EQ(pooled[i].a, serial[i].a);
        EXPECT_EQ(pooled[i].b, serial[i].b);
        EXPECT_EQ(pooled[i].c, serial[i].c);
        EXPECT_EQ(pooled[i].curvature_per_m, serial[i].curvature_per_m);
        EXPECT_EQ(pooled[i].residual_rms_m, serial[i].residual_rms_m);
    }
}

TEST(FitThreadPool, HandlesEmptyInput) {
    FitThreadPool pool(2);
    auto results = pool.fit_lines({});
    EXPECT_TRUE(results.empty());
}

TEST(FitThreadPool, ZeroThreadsSelectsAutomaticCount) {
    FitThreadPool pool(0);
    std::vector<FilterResult> lines = {make_line(-0.005, 0.0, 0.0, 1)};
    auto results = pool.fit_lines(lines);
    ASSERT_EQ(results.size(), 1u);
    EXPECT_TRUE(results[0].valid);
}

}  // namespace
}  // namespace ssim::analysis
