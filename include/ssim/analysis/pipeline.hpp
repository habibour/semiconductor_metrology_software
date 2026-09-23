#pragma once

// PRD §8.4 steps 2-10, composed into one entry point. Step 1 (collecting
// sample blocks off the scan queue) is the caller's job — the composition
// root (equipment_cli) drains the queue into SampleBlocks and hands them
// here. This is what keeps the ten-step pipeline testable stage by stage
// (each stage already has its own unit tests) while still giving callers
// one function to run the whole thing.
//
// k0 (the pre-coating curvature to subtract) is read from
// config.wafer.truth.initial_curvature_1_per_m — Day 2 has no separate
// pre-scan step, so the machine "knows" k0 the same way the simulator's
// forward model does (PRD §8.2).

#include <vector>

#include "ssim/analysis/combine.hpp"
#include "ssim/analysis/fit_pool.hpp"
#include "ssim/analysis/quality_gates.hpp"
#include "ssim/analysis/types.hpp"
#include "ssim/analysis/wafer_map.hpp"
#include "ssim/core/config.hpp"
#include "ssim/core/scan_types.hpp"

namespace ssim::analysis {

struct PipelineResult {
    std::vector<FilterResult> filtered_lines;  // for the sample CSV
    std::vector<LineFitResult> line_fits;       // for the per-line CSV
    CombinedResult combined;
    double stress_pa = 0.0;
    double stress_unc_pa = 0.0;
    double max_fit_rms_m = 0.0;
    double removed_fraction_overall = 0.0;
    bool any_dropout = false;
    QualityGateResult quality;
    WaferMap map;
};

PipelineResult run_pipeline(const std::vector<ssim::core::SampleBlock>& lines,
                            const ssim::core::Config& config, FitThreadPool& pool);

}  // namespace ssim::analysis
