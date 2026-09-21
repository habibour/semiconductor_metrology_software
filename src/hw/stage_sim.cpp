#include "ssim/hw/stage_sim.hpp"

#include <chrono>
#include <cmath>
#include <thread>

namespace ssim::hw {

StageSim::StageSim(double speed_mm_per_s, double realtime_factor)
    : speed_mm_per_s_(speed_mm_per_s), realtime_factor_(realtime_factor) {}

void StageSim::set_line(double angle_rad) {
    angle_rad_ = angle_rad;
    position_m_ = 0.0;
}

void StageSim::move_to(double position_m) {
    const double distance_mm = std::abs(position_m - position_m_) * 1000.0;
    if (realtime_factor_ > 0.0 && speed_mm_per_s_ > 0.0) {
        const double duration_s = distance_mm / speed_mm_per_s_ / realtime_factor_;
        std::this_thread::sleep_for(std::chrono::duration<double>(duration_s));
    }
    position_m_ = position_m;
}

}  // namespace ssim::hw
