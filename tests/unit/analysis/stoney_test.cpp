#include "ssim/analysis/stoney.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <random>

#include "ssim/analysis/combine.hpp"
#include "ssim/analysis/edge_exclusion.hpp"
#include "ssim/analysis/line_fit.hpp"
#include "ssim/analysis/outlier_rejection.hpp"
#include "ssim/hw/wafer_model.hpp"

namespace ssim::analysis {
namespace {

ssim::core::WaferConfig nominal_config() {
    ssim::core::WaferConfig cfg;
    cfg.diameter_mm = 300.0;
    cfg.thickness_um = 775.0;
    cfg.film_thickness_um = 1.0;
    cfg.biaxial_modulus_gpa = 180.5;
    cfg.truth.stress_mpa = -180.0;
    cfg.truth.initial_curvature_1_per_m = 0.002;
    cfg.truth.tilt_x_um_per_mm = 0.8;
    cfg.truth.tilt_y_um_per_mm = -0.3;
    cfg.truth.anisotropy = 0.05;
    cfg.seed = 777;
    return cfg;
}

// Generates 6 evenly-spaced noisy lines from the Day 1 WaferModel and runs
// them through the full edge-exclusion -> outlier -> fit -> combine ->
// stoney chain, exactly the "wired end-to-end" UT-STONEY-1 day-2-plan.md
// calls for.
double recover_stress_mpa(const ssim::core::WaferConfig& cfg, double noise_sigma_m,
                          std::uint64_t noise_seed) {
    ssim::hw::WaferModel model(cfg);
    const double radius_m = cfg.diameter_mm * 1e-3 / 2.0;
    constexpr int kLines = 6;
    constexpr int kPointsPerMm = 40;
    const int n_points = static_cast<int>(cfg.diameter_mm * kPointsPerMm) + 1;

    std::mt19937_64 rng(noise_seed);
    // normal_distribution requires sigma > 0, so zero noise draws nothing.
    std::normal_distribution<double> noise(0.0, noise_sigma_m > 0.0 ? noise_sigma_m : 1.0);
    const bool noisy = noise_sigma_m > 0.0;

    std::vector<LineFitResult> fits;
    for (int line = 0; line < kLines; ++line) {
        const double theta = M_PI * static_cast<double>(line) / kLines;
        LineSamples raw;
        raw.angle_rad = theta;
        for (int p = 0; p < n_points; ++p) {
            const double s = -radius_m + 2.0 * radius_m * static_cast<double>(p) / (n_points - 1);
            raw.s_m.push_back(s);
            raw.z_m.push_back(model.height_m(theta, s) + (noisy ? noise(rng) : 0.0));
        }
        auto filtered = apply_edge_exclusion(raw, /*edge_exclusion_mm=*/3.0);
        filtered = apply_outlier_rejection(filtered, /*mad_k=*/6.0);
        fits.push_back(fit_line(filtered));
    }

    auto combined = combine_lines(fits);
    const double m_s_pa = cfg.biaxial_modulus_gpa * 1e9;
    const double t_s_m = cfg.thickness_um * 1e-6;
    const double t_f_m = cfg.film_thickness_um * 1e-6;
    const double k0 = cfg.truth.initial_curvature_1_per_m;
    const double stress_pa =
        stoney_stress_pa(combined.mean_curvature_per_m, k0, m_s_pa, t_s_m, t_f_m);
    return stress_pa * 1e-6;
}

// UT-STONEY-1: hidden truth of -180 MPa recovered within 2% on the nominal
// wafer, with realistic (PRD §8.3 default 0.5 um) sensor noise.
TEST(Stoney, RecoversHiddenTruthWithinTwoPercent) {
    auto cfg = nominal_config();
    const double recovered_mpa = recover_stress_mpa(cfg, /*noise_sigma_m=*/0.5e-6, /*seed=*/42);

    EXPECT_NEAR(recovered_mpa, -180.0, std::abs(-180.0) * 0.02);
}

TEST(Stoney, NoiseFreeRecoversExactly) {
    auto cfg = nominal_config();
    const double recovered_mpa = recover_stress_mpa(cfg, /*noise_sigma_m=*/0.0, /*seed=*/1);
    EXPECT_NEAR(recovered_mpa, -180.0, 0.5);  // fit/interpolation floor, not noise
}

// UT-STONEY-2: sign convention — convex up (centre-high, compressive) gives
// negative curvature and negative (compressive) stress; concave up
// (edges-high, tensile) gives positive curvature and positive stress.
TEST(Stoney, SignConventionMatchesConcaveConvex) {
    auto compressive = nominal_config();
    compressive.truth.stress_mpa = -180.0;
    compressive.truth.tilt_x_um_per_mm = 0.0;
    compressive.truth.tilt_y_um_per_mm = 0.0;
    compressive.truth.anisotropy = 0.0;
    compressive.truth.initial_curvature_1_per_m = 0.0;
    EXPECT_LT(recover_stress_mpa(compressive, 0.0, 1), 0.0);

    auto tensile = compressive;
    tensile.truth.stress_mpa = 180.0;
    EXPECT_GT(recover_stress_mpa(tensile, 0.0, 1), 0.0);
}

}  // namespace
}  // namespace ssim::analysis
