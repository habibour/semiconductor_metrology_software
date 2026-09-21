#include "ssim/hw/wafer_model.hpp"

#include <cmath>

namespace ssim::hw {

namespace {
constexpr double kMpaToPa = 1e6;
constexpr double kUmToM = 1e-6;
constexpr double kMmToM = 1e-3;
constexpr double kGpaToPa = 1e9;

// tilt is given as um per mm, i.e. (1e-6 m) / (1e-3 m) = a dimensionless
// slope (m/m).
constexpr double kTiltUmPerMmToSlope = 1e-3;
}  // namespace

WaferModel::WaferModel(const ssim::core::WaferConfig& config) : seed_(config.seed) {
    const double sigma_pa = config.truth.stress_mpa * kMpaToPa;
    const double t_f_m = config.film_thickness_um * kUmToM;
    const double t_s_m = config.thickness_um * kUmToM;
    const double m_s_pa = config.biaxial_modulus_gpa * kGpaToPa;

    truth_.stress_pa = sigma_pa;
    truth_.initial_curvature_per_m = config.truth.initial_curvature_1_per_m;
    truth_.tilt_x = config.truth.tilt_x_um_per_mm * kTiltUmPerMmToSlope;
    truth_.tilt_y = config.truth.tilt_y_um_per_mm * kTiltUmPerMmToSlope;
    truth_.anisotropy = config.truth.anisotropy;
    truth_.diameter_m = config.diameter_mm * kMmToM;

    // PRD §8.2 forward model.
    truth_.film_curvature_per_m = 6.0 * sigma_pa * t_f_m / (m_s_pa * t_s_m * t_s_m);
    truth_.total_curvature_per_m = truth_.initial_curvature_per_m + truth_.film_curvature_per_m;
    truth_.kx_per_m = truth_.total_curvature_per_m * (1.0 - truth_.anisotropy / 2.0);
    truth_.ky_per_m = truth_.total_curvature_per_m * (1.0 + truth_.anisotropy / 2.0);
}

double WaferModel::curvature_at_m(double theta_rad) const {
    const double c = std::cos(theta_rad);
    const double s = std::sin(theta_rad);
    return truth_.kx_per_m * c * c + truth_.ky_per_m * s * s;
}

double WaferModel::height_m(double theta_rad, double s_m) const {
    const double tilt_along_line =
        truth_.tilt_x * std::cos(theta_rad) + truth_.tilt_y * std::sin(theta_rad);
    const double k = curvature_at_m(theta_rad);
    return tilt_along_line * s_m + 0.5 * k * s_m * s_m;
}

}  // namespace ssim::hw
