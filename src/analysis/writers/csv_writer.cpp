#include "ssim/analysis/writers/csv_writer.hpp"

#include <cmath>
#include <fstream>
#include <limits>

namespace ssim::analysis {

namespace {
constexpr double kRadToDeg = 180.0 / 3.14159265358979323846;
constexpr double kMToMm = 1000.0;
constexpr double kMToUm = 1e6;
constexpr double kSlopeToUmPerMm = 1000.0;  // (1e-6 m)/(1e-3 m) = 1e-3 slope per um/mm unit

std::string format_or_blank(double value) {
    if (!std::isfinite(value)) {
        return "";
    }
    return std::to_string(value);
}
}  // namespace

ssim::core::Result<std::filesystem::path> write_line_csv(const std::filesystem::path& wafer_dir,
                                                          const std::string& wafer_id,
                                                          const PipelineResult& result) {
    const std::filesystem::path path = wafer_dir / "lines.csv";
    if (std::filesystem::exists(path)) {
        return ssim::core::Result<std::filesystem::path>::err(
            {kWriteErrorExitCode, "lines.csv already exists: " + path.string()});
    }
    std::ofstream out(path);
    if (!out) {
        return ssim::core::Result<std::filesystem::path>::err(
            {kWriteErrorExitCode, "cannot open for writing: " + path.string()});
    }

    out << "wafer_id,line_index,angle_deg,n_samples,n_removed,curvature_1_per_m,radius_m,"
          "se_curvature,rms_um,tilt_um_per_mm,offset_um\n";
    for (std::size_t i = 0; i < result.line_fits.size(); ++i) {
        const LineFitResult& fit = result.line_fits[i];
        const FilterResult& filtered = result.filtered_lines[i];
        const double radius_m = fit.curvature_per_m != 0.0 ? 1.0 / fit.curvature_per_m
                                                            : std::numeric_limits<double>::infinity();
        out << wafer_id << ',' << i << ',' << (fit.angle_rad * kRadToDeg) << ','
            << filtered.samples.size() << ',' << filtered.removed_count << ','
            << fit.curvature_per_m << ',' << format_or_blank(radius_m) << ','
            << fit.se_curvature_per_m << ',' << (fit.residual_rms_m * kMToUm) << ','
            << (fit.b * kSlopeToUmPerMm) << ',' << (fit.c * kMToUm) << '\n';
    }
    return ssim::core::Result<std::filesystem::path>::ok(path);
}

ssim::core::Result<std::filesystem::path> write_sample_csv(const std::filesystem::path& wafer_dir,
                                                            const std::string& wafer_id,
                                                            const PipelineResult& result,
                                                            int decimation) {
    const std::filesystem::path path = wafer_dir / "samples.csv";
    if (std::filesystem::exists(path)) {
        return ssim::core::Result<std::filesystem::path>::err(
            {kWriteErrorExitCode, "samples.csv already exists: " + path.string()});
    }
    std::ofstream out(path);
    if (!out) {
        return ssim::core::Result<std::filesystem::path>::err(
            {kWriteErrorExitCode, "cannot open for writing: " + path.string()});
    }

    out << "wafer_id,line_index,s_mm,z_um_raw,z_um_clean,flag\n";
    const int step = decimation > 0 ? decimation : 1;
    for (std::size_t i = 0; i < result.filtered_lines.size(); ++i) {
        const auto& samples = result.filtered_lines[i].samples;
        int kept_seen = 0;
        for (const auto& s : samples) {
            const bool kept = s.flag == SampleFlag::kKept;
            if (kept) {
                if (kept_seen % step != 0) {
                    ++kept_seen;
                    continue;
                }
                ++kept_seen;
            }
            const double z_raw_um = s.flag == SampleFlag::kDropped ? std::nan("") : s.z_m * kMToUm;
            const double z_clean_um = kept ? s.z_m * kMToUm : std::nan("");
            out << wafer_id << ',' << i << ',' << (s.s_m * kMToMm) << ','
                << format_or_blank(z_raw_um) << ',' << format_or_blank(z_clean_um) << ','
                << static_cast<int>(s.flag) << '\n';
        }
    }
    return ssim::core::Result<std::filesystem::path>::ok(path);
}

}  // namespace ssim::analysis
