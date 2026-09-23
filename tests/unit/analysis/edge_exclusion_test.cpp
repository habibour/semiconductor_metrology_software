#include "ssim/analysis/edge_exclusion.hpp"

#include <gtest/gtest.h>

#include <cmath>

namespace ssim::analysis {
namespace {

LineSamples make_line(double edge_to_edge_m, int n) {
    LineSamples line;
    line.angle_rad = 0.0;
    const double half = edge_to_edge_m / 2.0;
    for (int i = 0; i < n; ++i) {
        const double s = -half + edge_to_edge_m * static_cast<double>(i) / (n - 1);
        line.s_m.push_back(s);
        line.z_m.push_back(0.0);
    }
    return line;
}

TEST(EdgeExclusion, TrimsBothEnds) {
    LineSamples line = make_line(/*edge_to_edge_m=*/0.3, 31);  // 10 mm spacing, -150..150 mm
    auto result = apply_edge_exclusion(line, /*edge_exclusion_mm=*/3.0);

    EXPECT_EQ(result.samples.front().flag, SampleFlag::kEdgeExcluded);
    EXPECT_EQ(result.samples.back().flag, SampleFlag::kEdgeExcluded);
    // -150mm and 150mm are within 3mm of the ends; -140 and 140 are not.
    for (const auto& s : result.samples) {
        const double s_mm = s.s_m * 1000.0;
        const bool near_edge = s_mm < -147.0 || s_mm > 147.0;
        EXPECT_EQ(s.flag == SampleFlag::kEdgeExcluded, near_edge) << "s_mm=" << s_mm;
    }
}

TEST(EdgeExclusion, ZeroExclusionKeepsEverything) {
    LineSamples line = make_line(0.3, 11);
    auto result = apply_edge_exclusion(line, 0.0);
    for (const auto& s : result.samples) {
        EXPECT_EQ(s.flag, SampleFlag::kKept);
    }
    EXPECT_EQ(result.removed_count, 0u);
}

TEST(EdgeExclusion, NanHeightIsFlaggedDropped) {
    LineSamples line = make_line(0.3, 11);
    line.z_m[5] = std::nan("");
    auto result = apply_edge_exclusion(line, 0.0);
    EXPECT_EQ(result.samples[5].flag, SampleFlag::kDropped);
}

}  // namespace
}  // namespace ssim::analysis
