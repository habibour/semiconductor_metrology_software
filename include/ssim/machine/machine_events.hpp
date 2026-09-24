#pragma once

// Thread-safety: plain, copyable value types; the wafer map is shared and
// immutable (shared_ptr<const>), so it is safe to hold on any thread.
//
// Events that only ssim_machine can produce because their payload comes from
// ssim_analysis, which ssim_core must not include (PRD §6.2). The
// analysis-free result numbers travel in ssim::core::WaferResultReady.

#include <filesystem>
#include <memory>
#include <string>

#include "ssim/analysis/wafer_map.hpp"

namespace ssim::machine {

// Published together with (and just after) WaferResultReady, only for a
// wafer whose result was usable. Heights are in metres (WaferMap's unit),
// NaN outside the wafer.
struct WaferMapReady {
    std::string wafer_id;
    std::shared_ptr<const ssim::analysis::WaferMap> map;
    double edge_exclusion_mm = 0.0;
};

// Published last for every processed wafer (also when a quality alarm
// stopped the result): the output files are on disk (ok) or the write failed.
struct WaferOutputsWritten {
    std::string wafer_id;
    std::filesystem::path dir;
    bool ok = false;
};

}  // namespace ssim::machine
