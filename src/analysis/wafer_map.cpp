#include "ssim/analysis/wafer_map.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace ssim::analysis {

namespace {
constexpr double kPi = 3.14159265358979323846;

struct DirectedRay {
    double angle_rad;  // in [0, 2*pi)
    double a, b, c;    // z = a*r^2 + sign*b*r + c, sign already folded into b here
};

double normalize_angle(double theta) {
    while (theta < 0.0) theta += 2.0 * kPi;
    while (theta >= 2.0 * kPi) theta -= 2.0 * kPi;
    return theta;
}

double eval_ray(const DirectedRay& ray, double r_m) {
    return ray.a * r_m * r_m + ray.b * r_m + ray.c;
}
}  // namespace

WaferMap build_wafer_map(const std::vector<LineFitResult>& fits, double diameter_m,
                         double grid_mm) {
    WaferMap map;
    map.grid_mm = grid_mm;
    map.diameter_m = diameter_m;

    const double radius_mm = diameter_m * 1000.0 / 2.0;
    const int half_n = static_cast<int>(std::ceil(radius_mm / grid_mm));
    map.width = 2 * half_n + 1;
    map.height = map.width;
    map.heights_m.assign(static_cast<std::size_t>(map.width) * static_cast<std::size_t>(map.height),
                         std::numeric_limits<double>::quiet_NaN());

    std::vector<DirectedRay> rays;
    for (const auto& fit : fits) {
        if (!fit.valid) {
            continue;
        }
        rays.push_back({normalize_angle(fit.angle_rad), fit.a, fit.b, fit.c});
        rays.push_back({normalize_angle(fit.angle_rad + kPi), fit.a, -fit.b, fit.c});
    }
    if (rays.empty()) {
        return map;
    }
    std::sort(rays.begin(), rays.end(),
              [](const DirectedRay& x, const DirectedRay& y) { return x.angle_rad < y.angle_rad; });

    for (int j = 0; j < map.height; ++j) {
        const double y_mm = (j - half_n) * grid_mm;
        for (int i = 0; i < map.width; ++i) {
            const double x_mm = (i - half_n) * grid_mm;
            const double r_mm = std::hypot(x_mm, y_mm);
            if (r_mm > radius_mm) {
                continue;  // stays NaN, outside the wafer
            }
            const double r_m = r_mm / 1000.0;
            const double theta = normalize_angle(std::atan2(y_mm, x_mm));

            // Find the bracketing rays (rays is sorted, wraps at 2*pi).
            std::size_t hi_idx = 0;
            while (hi_idx < rays.size() && rays[hi_idx].angle_rad < theta) {
                ++hi_idx;
            }
            const std::size_t lo_idx = (hi_idx == 0) ? rays.size() - 1 : hi_idx - 1;
            const DirectedRay& lo = rays[lo_idx];
            const DirectedRay& hi = rays[hi_idx % rays.size()];

            double span = hi.angle_rad - lo.angle_rad;
            double offset = theta - lo.angle_rad;
            if (hi_idx == rays.size()) {  // wrapped past the last ray, back to the first
                span += 2.0 * kPi;
            }
            if (offset < 0.0) {
                offset += 2.0 * kPi;
            }
            const double frac = (span > 0.0) ? (offset / span) : 0.0;

            const double z_lo = eval_ray(lo, r_m);
            const double z_hi = eval_ray(hi, r_m);
            const std::size_t idx =
                static_cast<std::size_t>(j) * static_cast<std::size_t>(map.width) +
                static_cast<std::size_t>(i);
            map.heights_m[idx] = z_lo + frac * (z_hi - z_lo);
        }
    }
    return map;
}

}  // namespace ssim::analysis
