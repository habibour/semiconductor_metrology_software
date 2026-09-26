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

#include "ssim/core/spsc_ring_queue.hpp"

namespace ssim::core {

struct SampleBlock {
    std::size_t line_index = 0;
    double angle_rad = 0.0;
    std::vector<double> positions_m;
    std::vector<double> heights_m;  // nan where a sample was dropped (FaultType::kDropout)
    std::vector<std::chrono::steady_clock::time_point> timestamps;
};

// The sample path: the scan thread is the only producer and the processing
// worker the only consumer, which is what lets it be the lock-free ring (v2,
// D-09). Swapping back to BoundedQueue<SampleBlock> is a one-line change here.
using SampleQueue = SpscRingQueue<SampleBlock>;

}  // namespace ssim::core
