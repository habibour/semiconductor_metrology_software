#include "ssim/analysis/pipeline.hpp"

#include <algorithm>

#include "ssim/analysis/edge_exclusion.hpp"
#include "ssim/analysis/outlier_rejection.hpp"
#include "ssim/analysis/stoney.hpp"
#include "ssim/analysis/uncertainty.hpp"

namespace ssim::analysis {

namespace {
constexpr double kUmToM = 1e-6;
constexpr double kMpaToPa = 1e6;
constexpr double kGpaToPa = 1e9;
}  // namespace

PipelineResult run_pipeline(const std::vector<ssim::core::SampleBlock>& lines,
                            const ssim::core::Config& config, FitThreadPool& pool) {
    PipelineResult result;

    std::size_t total_samples = 0;
    std::size_t total_removed = 0;
    std::size_t total_outliers = 0;
    for (const auto& block : lines) {
        LineSamples raw{block.angle_rad, block.positions_m, block.heights_m};
        FilterResult filtered = apply_edge_exclusion(raw, config.scan.edge_exclusion_mm);
        filtered = apply_outlier_rejection(filtered, config.analysis.outlier_mad_k);

        for (const auto& s : filtered.samples) {
            if (s.flag == SampleFlag::kDropped) {
                result.any_dropout = true;
            } else if (s.flag == SampleFlag::kOutlier) {
                ++total_outliers;
            }
        }
        total_samples += filtered.samples.size();
        total_removed += filtered.removed_count;
        result.filtered_lines.push_back(std::move(filtered));
    }
    result.removed_fraction_overall =
        total_samples > 0 ? static_cast<double>(total_removed) / static_cast<double>(total_samples)
                          : 0.0;
    result.outlier_removed_fraction =
        total_samples > 0 ? static_cast<double>(total_outliers) / static_cast<double>(total_samples)
                          : 0.0;

    result.line_fits = pool.fit_lines(result.filtered_lines);

    for (const auto& fit : result.line_fits) {
        if (fit.valid) {
            result.max_fit_rms_m = std::max(result.max_fit_rms_m, fit.residual_rms_m);
        }
    }

    result.combined = combine_lines(result.line_fits);

    const double m_s_pa = config.wafer.biaxial_modulus_gpa * kGpaToPa;
    const double t_s_m = config.wafer.thickness_um * kUmToM;
    const double t_f_m = config.wafer.film_thickness_um * kUmToM;
    const double k0 = config.wafer.truth.initial_curvature_1_per_m;

    result.stress_pa =
        stoney_stress_pa(result.combined.mean_curvature_per_m, k0, m_s_pa, t_s_m, t_f_m);
    result.stress_unc_pa = stress_uncertainty_pa(result.line_fits, m_s_pa, t_s_m, t_f_m);

    const double fit_rms_limit_m = config.analysis.fit_rms_limit_um * kUmToM;
    const double spec_low_pa = config.analysis.stress_spec_low_mpa * kMpaToPa;
    const double spec_high_pa = config.analysis.stress_spec_high_mpa * kMpaToPa;
    const double plausible_low_pa = config.analysis.stress_plausible_low_mpa * kMpaToPa;
    const double plausible_high_pa = config.analysis.stress_plausible_high_mpa * kMpaToPa;
    result.quality = evaluate_quality(result.max_fit_rms_m, fit_rms_limit_m, result.stress_pa,
                                      plausible_low_pa, plausible_high_pa, spec_low_pa,
                                      spec_high_pa);

    if (result.quality.issue == QualityIssue::kNone) {
        result.map = build_wafer_map(result.line_fits, config.wafer.diameter_mm * 1e-3,
                                     config.analysis.map_grid_mm);
    }

    return result;
}

}  // namespace ssim::analysis
