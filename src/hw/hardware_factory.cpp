#include "ssim/hw/hardware_factory.hpp"

#include "ssim/hw/laser_sensor_sim.hpp"
#include "ssim/hw/stage_sim.hpp"

namespace ssim::hw {

namespace {
constexpr double kUmToM = 1e-6;
}  // namespace

SimulatedHardware make_simulated_hardware(const ssim::core::ScanConfig& scan_config,
                                          const ssim::core::NoiseConfig& noise_config,
                                          const WaferModel& wafer_model) {
    SimulatedHardware hw;
    auto stage =
        std::make_unique<StageSim>(scan_config.speed_mm_per_s, scan_config.realtime_factor);
    auto laser = std::make_unique<LaserSensorSim>(
        *stage, wafer_model, noise_config.sigma_um * kUmToM, wafer_model.seed());
    hw.stage = std::move(stage);
    hw.laser = std::move(laser);
    return hw;
}

}  // namespace ssim::hw
