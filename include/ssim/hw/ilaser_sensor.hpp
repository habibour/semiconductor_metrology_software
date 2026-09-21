#pragma once

// FR-HW-1: see istage.hpp for the "no concrete device outside the hardware
// factory" rule.
//
// Thread-safety: implementations document their own thread-safety;
// LaserSensorSim (the Day 1 implementation) is "Owned by the scan thread".

namespace ssim::hw {

class ILaserSensor {
public:
    virtual ~ILaserSensor() = default;

    // Reads height (metres) at the stage's current position: the wafer
    // model's ideal height plus tilt, noise and any active fault injection
    // (PRD §8.2 "measured = z(s) + noise (+ injected faults)").
    virtual double read_height_m() = 0;
};

}  // namespace ssim::hw
