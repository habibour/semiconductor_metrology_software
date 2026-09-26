// BM-ANALYSIS-1 / NFR-PERF-1 (PRD 10.2, 9): time to analyse one nominal wafer
// (6 lines x 40 points per mm on 300 mm = 72,006 samples) at real-time factor 0,
// by stage and end to end, for several thread-pool sizes. The wafer data comes
// from the simulated hardware, exactly as the scan thread would produce it.
//
//   ./bench_analysis         (Release build only)

#include <cstdio>
#include <vector>

#include "bench_common.hpp"
#include "ssim/analysis/edge_exclusion.hpp"
#include "ssim/analysis/fit_pool.hpp"
#include "ssim/analysis/line_fit.hpp"
#include "ssim/analysis/outlier_rejection.hpp"
#include "ssim/analysis/pipeline.hpp"
#include "ssim/analysis/wafer_map.hpp"
#include "ssim/core/config.hpp"
#include "ssim/core/scan_types.hpp"
#include "ssim/hw/hardware_factory.hpp"
#include "ssim/hw/wafer_model.hpp"

namespace {

using ssim::bench::median;

std::vector<ssim::core::SampleBlock> make_nominal_wafer(const ssim::core::Config& config) {
    ssim::hw::WaferModel wafer_model(config.wafer);
    auto hardware = ssim::hw::make_simulated_hardware(config.scan, config.noise, wafer_model);
    const double radius_m = wafer_model.truth().diameter_m / 2.0;
    const int n_points = static_cast<int>(radius_m * 2.0 * 1000.0 * config.scan.points_per_mm) + 1;
    constexpr double kPi = 3.14159265358979323846;

    std::vector<ssim::core::SampleBlock> lines;
    for (int line = 0; line < config.scan.lines; ++line) {
        const double theta = kPi * line / config.scan.lines;
        hardware.stage->set_line(theta);
        ssim::core::SampleBlock block;
        block.line_index = static_cast<std::size_t>(line);
        block.angle_rad = theta;
        for (int p = 0; p < n_points; ++p) {
            const double s = -radius_m + 2.0 * radius_m * p / (n_points - 1);
            hardware.stage->move_to(s);
            block.positions_m.push_back(s);
            block.heights_m.push_back(hardware.laser->read_height_m());
        }
        lines.push_back(std::move(block));
    }
    return lines;
}

}  // namespace

int main() {
    ssim::bench::print_environment("bench_analysis: one nominal wafer, real-time factor 0");

    ssim::core::Config config;
    config.scan.realtime_factor = 0.0;
    const auto lines = make_nominal_wafer(config);
    std::size_t samples = 0;
    for (const auto& l : lines) samples += l.positions_m.size();
    std::printf("# wafer: %zu lines, %zu samples\n\n", lines.size(), samples);

    // ---- stage by stage, single thread -------------------------------------
    std::vector<ssim::analysis::FilterResult> edge_done;
    std::vector<ssim::analysis::FilterResult> outlier_done;
    const auto edge = ssim::bench::time_runs(15, [&] {
        edge_done.clear();
        for (const auto& b : lines) {
            ssim::analysis::LineSamples raw{b.angle_rad, b.positions_m, b.heights_m};
            edge_done.push_back(
                ssim::analysis::apply_edge_exclusion(raw, config.scan.edge_exclusion_mm));
        }
    });
    const auto outlier = ssim::bench::time_runs(15, [&] {
        outlier_done.clear();
        for (const auto& f : edge_done) {
            outlier_done.push_back(
                ssim::analysis::apply_outlier_rejection(f, config.analysis.outlier_mad_k));
        }
    });
    ssim::analysis::FitThreadPool one_thread(1);
    std::vector<ssim::analysis::LineFitResult> fits;
    const auto fit = ssim::bench::time_runs(15, [&] { fits = one_thread.fit_lines(outlier_done); });
    const auto map = ssim::bench::time_runs(15, [&] {
        (void)ssim::analysis::build_wafer_map(fits, config.wafer.diameter_mm * 1e-3,
                                              config.analysis.map_grid_mm);
    });

    std::printf("| stage (single thread) | median ms |\n|---|---|\n");
    std::printf("| edge exclusion, 6 lines | %.3f |\n", median(edge) * 1e3);
    std::printf("| outlier rejection, 6 lines | %.3f |\n", median(outlier) * 1e3);
    std::printf("| line fits, 6 lines | %.3f |\n", median(fit) * 1e3);
    std::printf("| wafer map | %.3f |\n\n", median(map) * 1e3);

    // ---- whole pipeline versus pool size -----------------------------------
    std::printf("| pool threads | pipeline median ms | min | max |\n|---|---|---|---|\n");
    for (std::size_t threads : {std::size_t{1}, std::size_t{2}, std::size_t{4}, std::size_t{6}}) {
        ssim::analysis::FitThreadPool pool(threads);
        const auto runs = ssim::bench::time_runs(
            15, [&] { (void)ssim::analysis::run_pipeline(lines, config, pool); });
        std::printf("| %zu | %.3f | %.3f | %.3f |\n", threads, median(runs) * 1e3,
                    ssim::bench::min_of(runs) * 1e3, ssim::bench::max_of(runs) * 1e3);
        std::fflush(stdout);
    }
    return 0;
}
