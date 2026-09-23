#include "ssim/analysis/writers/json_writer.hpp"

#include <fstream>

#include <nlohmann/json.hpp>

namespace ssim::analysis {

namespace {
constexpr double kPaToMpa = 1e-6;
}  // namespace

ssim::core::Result<std::filesystem::path> write_json_summary(const std::filesystem::path& wafer_dir,
                                                              const PipelineResult& result,
                                                              const WaferRunMeta& meta) {
    const std::filesystem::path path = wafer_dir / "summary.json";
    if (std::filesystem::exists(path)) {
        return ssim::core::Result<std::filesystem::path>::err(
            {kWriteErrorExitCode, "summary.json already exists: " + path.string()});
    }

    const bool has_number = result.quality.issue == QualityIssue::kNone;
    const nlohmann::json stress_mpa =
        has_number ? nlohmann::json(result.stress_pa * kPaToMpa) : nlohmann::json(nullptr);
    const nlohmann::json stress_unc_mpa =
        has_number ? nlohmann::json(result.stress_unc_pa * kPaToMpa) : nlohmann::json(nullptr);

    nlohmann::json j;
    j["run_id"] = meta.run_id;
    j["wafer_id"] = meta.wafer_id;
    j["slot"] = meta.slot;
    j["cassette_id"] = meta.cassette_id;
    j["result"] = {
        {"stress_mpa", stress_mpa},
        {"stress_unc_mpa", stress_unc_mpa},
        {"mean_curvature_1_per_m", result.combined.mean_curvature_per_m},
        {"anisotropy", result.combined.anisotropy},
        {"fit_rms_um_max", result.max_fit_rms_m * 1e6},
        {"removed_fraction", result.removed_fraction_overall},
        {"out_of_spec", result.quality.out_of_spec},
    };
    j["simulation_truth"] = {
        {"stress_mpa", meta.truth_stress_mpa},
        {"seed", meta.seed},
    };
    j["timing_ms"] = {
        {"scan", meta.scan_ms},
        {"analysis", meta.analysis_ms},
    };
    j["alarms"] = nlohmann::json::array();

    std::ofstream out(path);
    if (!out) {
        return ssim::core::Result<std::filesystem::path>::err(
            {kWriteErrorExitCode, "cannot open for writing: " + path.string()});
    }
    out << j.dump(2);
    return ssim::core::Result<std::filesystem::path>::ok(path);
}

}  // namespace ssim::analysis
