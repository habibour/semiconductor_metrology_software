#pragma once

// FR-HW-1: no code outside the hardware factory (src/hw/hardware_factory.cpp)
// names a concrete stage type — production code (controller, scan thread,
// CLI, Qt panel) talks only to IStage. Unit tests are the one deliberate
// exception: they construct StageSim directly to test it in isolation.
//
// Thread-safety: implementations document their own thread-safety; StageSim
// (the Day 1 implementation) is "Owned by the scan thread" — not safe to
// call from more than one thread at a time.

namespace ssim::hw {

class IStage {
public:
    virtual ~IStage() = default;

    // Selects the diametric line to scan next, at the given angle
    // (radians, measured per PRD §8.2's theta convention).
    virtual void set_line(double angle_rad) = 0;

    // Moves to position_m (metres, signed, relative to the wafer centre)
    // along the current line. Blocks for the time implied by the configured
    // speed and real-time factor (FR-HW-5): 0 returns immediately, 1.0
    // waits the realistic duration.
    virtual void move_to(double position_m) = 0;

    virtual double current_position_m() const = 0;
    virtual double current_angle_rad() const = 0;
};

}  // namespace ssim::hw
