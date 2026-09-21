#include "ssim/hw/wafer_model.hpp"

#include <gtest/gtest.h>

#include <cmath>

namespace ssim::hw {
namespace {

ssim::core::WaferConfig nominal_config() {
    ssim::core::WaferConfig cfg;
    cfg.diameter_mm = 300.0;
    cfg.thickness_um = 775.0;
    cfg.film_thickness_um = 1.0;
    cfg.biaxial_modulus_gpa = 180.5;
    cfg.truth.stress_mpa = -180.0;
    cfg.truth.initial_curvature_1_per_m = 0.002;
    cfg.truth.tilt_x_um_per_mm = 0.0;
    cfg.truth.tilt_y_um_per_mm = 0.0;
    cfg.truth.anisotropy = 0.0;
    cfg.seed = 777;
    return cfg;
}

// UT-STONEY-1 (forward-model half only — day-1-spec.md). Reproduces PRD
// §8.2's worked example by hand:
//   film curvature   ~ -0.00996 /m
//   total curvature  ~ -0.00796 /m  (k0 = 0.002 /m)
//   centre-lift (film alone, on a 300 mm wafer) ~ 112 um
//     = 0.5 * |film curvature| * (0.15 m)^2
TEST(WaferModel, ReproducesStoneyWorkedExample) {
    WaferModel model(nominal_config());
    const WaferTruth& t = model.truth();

    EXPECT_NEAR(t.film_curvature_per_m, -0.00996, 0.0001);
    EXPECT_NEAR(t.total_curvature_per_m, -0.00796, 0.0001);

    const double radius_m = t.diameter_m / 2.0;
    const double film_only_center_lift_m =
        0.5 * std::abs(t.film_curvature_per_m) * radius_m * radius_m;
    EXPECT_NEAR(film_only_center_lift_m * 1e6, 112.0, 1.0);  // micrometres, +-1um
}

TEST(WaferModel, SameSeedGivesIdenticalTruth) {
    auto config = nominal_config();
    config.seed = 424242;

    WaferModel a(config);
    WaferModel b(config);

    EXPECT_EQ(a.seed(), b.seed());
    EXPECT_DOUBLE_EQ(a.truth().film_curvature_per_m, b.truth().film_curvature_per_m);
    EXPECT_DOUBLE_EQ(a.truth().total_curvature_per_m, b.truth().total_curvature_per_m);
    EXPECT_EQ(a.seed(), 424242u);
}

TEST(WaferModel, IsotropicCurvatureIsAngleIndependent) {
    WaferModel model(nominal_config());  // anisotropy = 0
    const double k0 = model.curvature_at_m(0.0);
    const double k_quarter_pi = model.curvature_at_m(M_PI / 4.0);
    const double k_half_pi = model.curvature_at_m(M_PI / 2.0);

    EXPECT_NEAR(k0, k_quarter_pi, 1e-12);
    EXPECT_NEAR(k0, k_half_pi, 1e-12);
}

TEST(WaferModel, HeightAtCentreIsZeroWithNoTiltOrOffset) {
    WaferModel model(nominal_config());
    EXPECT_DOUBLE_EQ(model.height_m(0.0, 0.0), 0.0);
}

TEST(WaferModel, HeightFollowsParabolaAlongALine) {
    WaferModel model(nominal_config());
    const double k = model.curvature_at_m(0.0);
    const double s = 0.05;  // 5 cm from centre
    EXPECT_NEAR(model.height_m(0.0, s), 0.5 * k * s * s, 1e-12);
}

// PRD §8.2 sign convention: concave up (edges higher, centre lower for a
// compressive/negative-stress film) is negative curvature. This wafer's
// truth is sigma = -180 MPa (compressive), so the film term must be
// negative and (with a small k0) so must the total.
TEST(WaferModel, CompressiveStressGivesNegativeCurvature) {
    WaferModel model(nominal_config());
    EXPECT_LT(model.truth().film_curvature_per_m, 0.0);
    EXPECT_LT(model.truth().total_curvature_per_m, 0.0);
}

}  // namespace
}  // namespace ssim::hw
