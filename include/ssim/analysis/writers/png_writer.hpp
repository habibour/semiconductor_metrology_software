#pragma once

// FR-OUT-3 / PRD §8.5: PNG wafer map, no Qt dependency (renders via
// stb_image_write). Colour scale in micrometres of height, a colour bar,
// and a thin ring marking the edge-exclusion boundary. Day 2 scope note:
// pixel-rendered text (title, axis labels) is out of proportion to what
// this day needs — the wafer/stress identification that would go in a
// title lives in summary.json next to this file instead.

#include <filesystem>
#include <string>

#include "ssim/analysis/wafer_map.hpp"
#include "ssim/core/result.hpp"

namespace ssim::analysis {

ssim::core::Result<std::filesystem::path> write_wafer_map_png(
    const std::filesystem::path& wafer_dir, const WaferMap& map, double edge_exclusion_mm);

}  // namespace ssim::analysis
