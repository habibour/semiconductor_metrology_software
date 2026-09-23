#include "ssim/analysis/uncertainty.hpp"

#include <gtest/gtest.h>

#include <cmath>

namespace ssim::analysis {
namespace {

LineFitResult make_fit(double se_curvature_per_m) {
    LineFitResult fit;
    fit.valid = true;
    fit.se_curvature_per_m = se_curvature_per_m;
    return fit;
}

TEST(Uncertainty, PropagatesSeThroughStoney) {
    // Two lines with equal SE(k): SE(k_mean) = sqrt(2*se^2)/2 = se/sqrt(2).
    std::vector<LineFitResult> fits = {make_fit(1e-4), make_fit(1e-4)};
    constexpr double kMsPa = 180.5e9;
    constexpr double kTsM = 775e-6;
    constexpr double kTfM = 1e-6;

    const double unc_pa = stress_uncertainty_pa(fits, kMsPa, kTsM, kTfM);

    const double expected_se_k = 1e-4 / std::sqrt(2.0);
    const double expected_unc_pa = kMsPa * kTsM * kTsM * expected_se_k / (6.0 * kTfM);
    EXPECT_NEAR(unc_pa, expected_unc_pa, expected_unc_pa * 1e-9);
}

TEST(Uncertainty, IgnoresInvalidFits) {
    LineFitResult invalid;
    invalid.valid = false;
    invalid.se_curvature_per_m = 1e6;  // would dominate if wrongly included
    std::vector<LineFitResult> fits = {make_fit(1e-4), invalid};

    const double unc_pa = stress_uncertainty_pa(fits, 180.5e9, 775e-6, 1e-6);
    const double expected_unc_pa = 180.5e9 * 775e-6 * 775e-6 * 1e-4 / (6.0 * 1e-6);
    EXPECT_NEAR(unc_pa, expected_unc_pa, expected_unc_pa * 1e-9);
}

}  // namespace
}  // namespace ssim::analysis
