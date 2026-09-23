#include "ssim/analysis/wafer_map.hpp"

#include <gtest/gtest.h>

#include <cmath>

namespace ssim::analysis {
namespace {

LineFitResult flat_fit(double angle_rad, double c) {
    LineFitResult fit;
    fit.angle_rad = angle_rad;
    fit.a = 0.0;
    fit.b = 0.0;
    fit.c = c;
    fit.valid = true;
    return fit;
}

TEST(WaferMap, ConstantHeightEverywhereInsideTheWafer) {
    std::vector<LineFitResult> fits = {flat_fit(0.0, 5e-6), flat_fit(M_PI / 3.0, 5e-6),
                                       flat_fit(2.0 * M_PI / 3.0, 5e-6)};
    auto map = build_wafer_map(fits, /*diameter_m=*/0.3, /*grid_mm=*/5.0);

    int inside_count = 0;
    for (double v : map.heights_m) {
        if (std::isfinite(v)) {
            EXPECT_NEAR(v, 5e-6, 1e-15);
            ++inside_count;
        }
    }
    EXPECT_GT(inside_count, 0);
}

TEST(WaferMap, OutsideRadiusIsNan) {
    std::vector<LineFitResult> fits = {flat_fit(0.0, 0.0)};
    auto map = build_wafer_map(fits, 0.3, 5.0);

    // The four grid corners are outside a 150mm-radius wafer.
    EXPECT_TRUE(std::isnan(map.heights_m.front()));
    EXPECT_TRUE(std::isnan(map.heights_m.back()));
}

TEST(WaferMap, InterpolatesBetweenTwoDifferentLines) {
    std::vector<LineFitResult> fits = {flat_fit(0.0, 0.0), flat_fit(M_PI / 2.0, 10e-6)};
    auto map = build_wafer_map(fits, 0.3, 5.0);

    for (double v : map.heights_m) {
        if (std::isfinite(v)) {
            EXPECT_GE(v, -1e-9);
            EXPECT_LE(v, 10e-6 + 1e-9);
        }
    }
}

TEST(WaferMap, NoValidFitsProducesAllNan) {
    std::vector<LineFitResult> fits;
    auto map = build_wafer_map(fits, 0.3, 5.0);
    for (double v : map.heights_m) {
        EXPECT_TRUE(std::isnan(v));
    }
}

}  // namespace
}  // namespace ssim::analysis
