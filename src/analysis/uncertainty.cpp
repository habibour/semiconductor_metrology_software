#include "ssim/analysis/uncertainty.hpp"

#include <cmath>

namespace ssim::analysis {

double stress_uncertainty_pa(const std::vector<LineFitResult>& fits, double biaxial_modulus_pa,
                             double substrate_thickness_m, double film_thickness_m) {
    double sum_sq = 0.0;
    std::size_t n = 0;
    for (const auto& fit : fits) {
        if (!fit.valid) {
            continue;
        }
        sum_sq += fit.se_curvature_per_m * fit.se_curvature_per_m;
        ++n;
    }
    if (n == 0) {
        return 0.0;
    }
    const double se_k_mean = std::sqrt(sum_sq) / static_cast<double>(n);
    return biaxial_modulus_pa * substrate_thickness_m * substrate_thickness_m * se_k_mean /
           (6.0 * film_thickness_m);
}

}  // namespace ssim::analysis
