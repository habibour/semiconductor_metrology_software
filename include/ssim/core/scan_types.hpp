#pragma once

// Thread-safety: SampleBlock is a plain value type; safe to move across the
// sample queue between the scan thread (producer) and the processing pool
// (consumer).
//
// FR-SCN-2: what the scan thread pushes into the bounded sample queue —
// deliberately hardware-agnostic (no IStage/ILaserSensor reference) so it
// can live in ssim_core, which must not depend on ssim_hw (PRD §6.2).

#include <chrono>
#include <cstddef>
#include <vector>

namespace ssim::core {

struct SampleBlock {
    std::size_t line_index = 0;
    double angle_rad = 0.0;
    std::vector<double> positions_m;
    std::vector<double> heights_m;  // nan where a sample was dropped (FaultType::kDropout)
    std::vector<std::chrono::steady_clock::time_point> timestamps;
};

}  // namespace ssim::core
