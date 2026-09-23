#pragma once

// FR-OUT-1 / PRD §8.5: per-wafer JSON summary. FR-OUT-5's directory layout
// (results/<run_id>/<wafer_id>/) and never-overwrite rule are the caller's
// responsibility (the composition root creates that directory once per
// run and hands each writer the already-fresh wafer_dir); the writer's own
// job is just to fail loudly if the file it's asked to write already
// exists, as a second line of defence.

#include <cstdint>
#include <filesystem>
#include <string>

#include "ssim/analysis/pipeline.hpp"
#include "ssim/core/result.hpp"

namespace ssim::analysis {

struct WaferRunMeta {
    std::string run_id;
    std::string wafer_id;
    int slot = 0;
    std::string cassette_id;
    std::uint64_t seed = 0;
    double truth_stress_mpa = 0.0;  // simulation_truth (this is a simulator, PRD §8.5)
    long long scan_ms = 0;
    long long analysis_ms = 0;
};

ssim::core::Result<std::filesystem::path> write_json_summary(const std::filesystem::path& wafer_dir,
                                                              const PipelineResult& result,
                                                              const WaferRunMeta& meta);

}  // namespace ssim::analysis
