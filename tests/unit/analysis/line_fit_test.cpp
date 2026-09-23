#include "ssim/analysis/line_fit.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <random>

namespace ssim::analysis {
namespace {

FilterResult make_parabola(double a, double b, double c, int n, double half_span_m,
                           double noise_sigma_m = 0.0, std::uint64_t seed = 1) {
    FilterResult result;
    std::mt19937_64 rng(seed);
    std::normal_distribution<double> noise(0.0, noise_sigma_m);
    for (int i = 0; i < n; ++i) {
        const double s = -half_span_m + 2.0 * half_span_m * static_cast<double>(i) / (n - 1);
        const double z = a * s * s + b * s + c + noise(rng);
        result.samples.push_back(FilteredSample{s, z, SampleFlag::kKept});
    }
    return result;
}

// UT-FIT-1: noise-free parabola with tilt and offset, exact recovery.
TEST(LineFit, NoiseFreeParabolaRecoveredExactly) {
    auto filtered = make_parabola(/*a=*/-0.005, /*b=*/0.001, /*c=*/2e-6, 41, 0.15);
    auto fit = fit_line(filtered);

    ASSERT_TRUE(fit.valid);
    EXPECT_NEAR(fit.a, -0.005, 1e-12);
    EXPECT_NEAR(fit.b, 0.001, 1e-12);
    EXPECT_NEAR(fit.c, 2e-6, 1e-12);
    EXPECT_NEAR(fit.curvature_per_m, -0.01, 1e-12);
    EXPECT_NEAR(fit.residual_rms_m, 0.0, 1e-15);
}

// UT-FIT-2: noisy parabola — curvature within a statistical bound, and the
// reported SE matches the spread actually observed over repeated trials.
TEST(LineFit, NoisyParabolaCurvatureWithinBoundAndSeMatchesRepeatedTrials) {
    constexpr double kA = -0.005;
    constexpr double kSigma = 0.5e-6;  // 0.5 um, PRD §8.3 default
    constexpr int kTrials = 400;

    std::vector<double> curvatures;
    std::vector<double> reported_se;
    curvatures.reserve(kTrials);
    for (int t = 0; t < kTrials; ++t) {
        auto filtered = make_parabola(kA, 0.001, 0.0, 41, 0.15, kSigma, /*seed=*/1000 + t);
        auto fit = fit_line(filtered);
        ASSERT_TRUE(fit.valid);
        curvatures.push_back(fit.curvature_per_m);
        reported_se.push_back(fit.se_curvature_per_m);
    }

    double mean = 0.0;
    for (double k : curvatures) mean += k;
    mean /= kTrials;
    EXPECT_NEAR(mean, 2.0 * kA, 1e-4);  // within statistical bound of the true curvature

    double variance = 0.0;
    for (double k : curvatures) variance += (k - mean) * (k - mean);
    variance /= (kTrials - 1);
    const double observed_se = std::sqrt(variance);

    double mean_reported_se = 0.0;
    for (double se : reported_se) mean_reported_se += se;
    mean_reported_se /= kTrials;

    // The average reported SE should match the actually-observed spread to
    // within 20% (a loose but real check, not a magic-number tautology).
    EXPECT_NEAR(mean_reported_se, observed_se, observed_se * 0.2);
}

TEST(LineFit, TooFewPointsIsInvalid) {
    FilterResult filtered;
    filtered.samples.push_back(FilteredSample{0.0, 0.0, SampleFlag::kKept});
    filtered.samples.push_back(FilteredSample{0.01, 0.0, SampleFlag::kKept});
    auto fit = fit_line(filtered);
    EXPECT_FALSE(fit.valid);
}

}  // namespace
}  // namespace ssim::analysis
