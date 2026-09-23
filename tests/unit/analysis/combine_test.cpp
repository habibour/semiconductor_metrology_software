#include "ssim/analysis/combine.hpp"

#include <gtest/gtest.h>

#include <cmath>

namespace ssim::analysis {
namespace {

LineFitResult make_fit(double angle_rad, double curvature_per_m) {
    LineFitResult fit;
    fit.angle_rad = angle_rad;
    fit.curvature_per_m = curvature_per_m;
    fit.valid = true;
    return fit;
}

double curvature_at(double kx, double ky, double theta) {
    const double c = std::cos(theta);
    const double s = std::sin(theta);
    return kx * c * c + ky * s * s;
}

// UT-ANISO-1, N>=2: with lines evenly spaced over 180 degrees, the mean of
// k(theta) equals (kx+ky)/2 regardless of anisotropy (PRD §8.2).
TEST(Combine, TwoEvenlySpacedLinesAverageToKxPlusKyOverTwo) {
    constexpr double kKx = -0.012;
    constexpr double kKy = -0.008;
    std::vector<LineFitResult> fits = {
        make_fit(0.0, curvature_at(kKx, kKy, 0.0)),
        make_fit(M_PI / 2.0, curvature_at(kKx, kKy, M_PI / 2.0)),
    };

    auto combined = combine_lines(fits);

    EXPECT_NEAR(combined.mean_curvature_per_m, (kKx + kKy) / 2.0, 1e-12);
}

TEST(Combine, SixEvenlySpacedLinesAverageToKxPlusKyOverTwo) {
    constexpr double kKx = -0.012;
    constexpr double kKy = -0.008;
    std::vector<LineFitResult> fits;
    for (int i = 0; i < 6; ++i) {
        const double theta = M_PI * static_cast<double>(i) / 6.0;
        fits.push_back(make_fit(theta, curvature_at(kKx, kKy, theta)));
    }

    auto combined = combine_lines(fits);

    EXPECT_NEAR(combined.mean_curvature_per_m, (kKx + kKy) / 2.0, 1e-9);
}

// UT-ANISO-1, N=1: the bias is real, not hidden — a single line along the
// stiff/soft axis reports that axis's curvature, not the true mean.
TEST(Combine, SingleLineIsBiasedByAnisotropy) {
    constexpr double kKx = -0.012;
    constexpr double kKy = -0.008;
    std::vector<LineFitResult> fits = {make_fit(0.0, curvature_at(kKx, kKy, 0.0))};

    auto combined = combine_lines(fits);

    EXPECT_NEAR(combined.mean_curvature_per_m, kKx, 1e-12);
    EXPECT_GT(std::abs(combined.mean_curvature_per_m - (kKx + kKy) / 2.0), 1e-6);
}

TEST(Combine, AnisotropyIsMaxMinusMinOverMean) {
    std::vector<LineFitResult> fits = {make_fit(0.0, -0.008), make_fit(M_PI / 2.0, -0.012)};
    auto combined = combine_lines(fits);
    EXPECT_NEAR(combined.anisotropy, (-0.008 - (-0.012)) / -0.01, 1e-12);
}

}  // namespace
}  // namespace ssim::analysis
