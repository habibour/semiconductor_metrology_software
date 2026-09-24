#include "ssim/analysis/quality_gates.hpp"

#include <cmath>

namespace ssim::analysis {

QualityGateResult evaluate_quality(double max_fit_rms_m, double fit_rms_limit_m, double stress_pa,
                                   double plausible_low_pa, double plausible_high_pa,
                                   double spec_low_pa, double spec_high_pa) {
    QualityGateResult result;
    if (max_fit_rms_m > fit_rms_limit_m) {
        result.issue = QualityIssue::kPoorFit;
        return result;
    }
    if (!std::isfinite(stress_pa) || stress_pa < plausible_low_pa ||
        stress_pa > plausible_high_pa) {
        result.issue = QualityIssue::kImplausible;
        return result;
    }
    result.out_of_spec = stress_pa < spec_low_pa || stress_pa > spec_high_pa;
    return result;
}

}  // namespace ssim::analysis
