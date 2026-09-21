#pragma once

// Thread-safety: Owned by the scan thread. Not safe to call from more than
// one thread concurrently.
//
// FR-HW-3 / FR-HW-5: simulates a stage moving along a scan line at a
// configured speed. realtime_factor 0 updates the position with no delay
// (for tests and benchmarks); realtime_factor 1.0 sleeps for the realistic
// duration. This sleep only ever happens in production use (realtime_factor
// > 0); unit tests use realtime_factor 0 and so never sleep, keeping the
// tests deterministic and fast (CLAUDE.md §6.5).

#include "ssim/hw/istage.hpp"

namespace ssim::hw {

class StageSim final : public IStage {
public:
    StageSim(double speed_mm_per_s, double realtime_factor);

    void set_line(double angle_rad) override;
    void move_to(double position_m) override;
    double current_position_m() const override { return position_m_; }
    double current_angle_rad() const override { return angle_rad_; }

private:
    double speed_mm_per_s_;
    double realtime_factor_;
    double position_m_ = 0.0;
    double angle_rad_ = 0.0;
};

}  // namespace ssim::hw
