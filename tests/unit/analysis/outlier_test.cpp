#include <gtest/gtest.h>

#include "ssim/analysis/outlier_rejection.hpp"

namespace ssim::analysis {
namespace {

FilterResult make_kept(const std::vector<double>& z_values) {
    FilterResult result;
    for (double z : z_values) {
        result.samples.push_back(FilteredSample{0.0, z, SampleFlag::kKept});
    }
    return result;
}

TEST(OutlierRejection, FlagsAFarOutlier) {
    // Tight cluster around 0 plus one wild point.
    auto input = make_kept({0.0, 0.01, -0.01, 0.02, -0.02, 5.0});
    auto result = apply_outlier_rejection(input, /*mad_k=*/6.0);

    EXPECT_EQ(result.samples.back().flag, SampleFlag::kOutlier);
    for (std::size_t i = 0; i + 1 < result.samples.size(); ++i) {
        EXPECT_EQ(result.samples[i].flag, SampleFlag::kKept);
    }
    EXPECT_EQ(result.removed_count, 1u);
}

TEST(OutlierRejection, SkipsAlreadyExcludedSamples) {
    FilterResult input;
    input.samples.push_back(FilteredSample{0.0, 100.0, SampleFlag::kEdgeExcluded});
    input.samples.push_back(FilteredSample{0.0, 0.0, SampleFlag::kKept});
    input.samples.push_back(FilteredSample{0.0, 0.1, SampleFlag::kKept});

    auto result = apply_outlier_rejection(input, 6.0);

    // The edge-excluded sample's wild value must never influence the
    // median/MAD computed over kKept samples, nor change its own flag.
    EXPECT_EQ(result.samples[0].flag, SampleFlag::kEdgeExcluded);
}

TEST(OutlierRejection, UniformDataFlagsNothing) {
    auto input = make_kept({1.0, 1.0, 1.0, 1.0});
    auto result = apply_outlier_rejection(input, 6.0);
    for (const auto& s : result.samples) {
        EXPECT_EQ(s.flag, SampleFlag::kKept);
    }
}

}  // namespace
}  // namespace ssim::analysis
