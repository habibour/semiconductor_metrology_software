#pragma once

// FR-OUT-2 / PRD §8.5: per-line results and (optionally subsampled) sample
// CSVs, columns exactly as PRD §8.5 lists them, so the files open sanely in
// Excel.

#include <filesystem>
#include <string>

#include "ssim/analysis/pipeline.hpp"
#include "ssim/core/result.hpp"

namespace ssim::analysis {

ssim::core::Result<std::filesystem::path> write_line_csv(const std::filesystem::path& wafer_dir,
                                                         const std::string& wafer_id,
                                                         const PipelineResult& result);

// decimation: write every Nth kept sample (output.sample_decimation);
// edge-excluded/outlier/dropped samples are always written (so the file
// shows what was thrown away), only kKept rows are subsampled.
ssim::core::Result<std::filesystem::path> write_sample_csv(const std::filesystem::path& wafer_dir,
                                                           const std::string& wafer_id,
                                                           const PipelineResult& result,
                                                           int decimation);

}  // namespace ssim::analysis
