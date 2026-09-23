#pragma once

// FR-PRC-7 / PRD §8.4 steps 6 and 9: poor fit or a non-finite result raises
// an alarm and reports no number; an implausible stress raises an alarm;
// stress outside the specification window is a flag and event, not an
// alarm. kPoorFit and kNonFinite/kImplausible are mutually exclusive with
// out_of_spec — a result that triggers an alarm never also carries a
// number to flag as out-of-spec.

namespace ssim::analysis {

enum class QualityIssue { kNone, kPoorFit, kImplausible };

struct QualityGateResult {
    QualityIssue issue = QualityIssue::kNone;
    bool out_of_spec = false;
};

// max_fit_rms_m: the worst per-line residual RMS across all lines (PRD
// §8.4 step 6 gates on any line being poor, not just the mean).
QualityGateResult evaluate_quality(double max_fit_rms_m, double fit_rms_limit_m, double stress_pa,
                                   double plausible_low_pa, double plausible_high_pa,
                                   double spec_low_pa, double spec_high_pa);

}  // namespace ssim::analysis
