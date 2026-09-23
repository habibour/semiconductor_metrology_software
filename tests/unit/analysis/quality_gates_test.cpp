#include "ssim/analysis/quality_gates.hpp"

#include <gtest/gtest.h>

#include <cmath>

namespace ssim::analysis {
namespace {

constexpr double kRmsLimitM = 2e-6;
constexpr double kPlausibleLoPa = -5000e6;
constexpr double kPlausibleHiPa = 5000e6;
constexpr double kSpecLoPa = -400e6;
constexpr double kSpecHiPa = 400e6;

QualityGateResult evaluate(double rms_m, double stress_pa) {
    return evaluate_quality(rms_m, kRmsLimitM, stress_pa, kPlausibleLoPa, kPlausibleHiPa, kSpecLoPa,
                            kSpecHiPa);
}

TEST(QualityGates, GoodResultPassesAndIsInSpec) {
    auto result = evaluate(1e-6, -180e6);
    EXPECT_EQ(result.issue, QualityIssue::kNone);
    EXPECT_FALSE(result.out_of_spec);
}

TEST(QualityGates, PoorFitReportsNoNumber) {
    auto result = evaluate(3e-6, -180e6);
    EXPECT_EQ(result.issue, QualityIssue::kPoorFit);
}

TEST(QualityGates, NonFiniteIsImplausible) {
    auto result = evaluate(1e-6, std::nan(""));
    EXPECT_EQ(result.issue, QualityIssue::kImplausible);
}

TEST(QualityGates, OutsidePlausibilityWindowIsImplausible) {
    auto result = evaluate(1e-6, 6000e6);
    EXPECT_EQ(result.issue, QualityIssue::kImplausible);
}

TEST(QualityGates, OutsideSpecWindowIsFlaggedNotAlarmed) {
    auto result = evaluate(1e-6, 450e6);
    EXPECT_EQ(result.issue, QualityIssue::kNone);
    EXPECT_TRUE(result.out_of_spec);
}

}  // namespace
}  // namespace ssim::analysis
