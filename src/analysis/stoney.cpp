#include "ssim/analysis/stoney.hpp"

namespace ssim::analysis {

double stoney_stress_pa(double k_mean_per_m, double k0_per_m, double biaxial_modulus_pa,
                        double substrate_thickness_m, double film_thickness_m) {
    return biaxial_modulus_pa * substrate_thickness_m * substrate_thickness_m *
          (k_mean_per_m - k0_per_m) / (6.0 * film_thickness_m);
}

}  // namespace ssim::analysis
