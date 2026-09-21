#pragma once

// FR-HW-1: production code obtains hardware through this factory and this
// factory alone; it is the only translation unit allowed to name a
// concrete simulated device type.

#include <memory>

#include "ssim/core/config.hpp"
#include "ssim/hw/ilaser_sensor.hpp"
#include "ssim/hw/istage.hpp"
#include "ssim/hw/wafer_model.hpp"

namespace ssim::hw {

struct SimulatedHardware {
    std::unique_ptr<IStage> stage;
    std::unique_ptr<ILaserSensor> laser;
};

// laser holds a non-owning reference to *stage internally; the returned
// SimulatedHardware keeps both alive together for exactly this reason —
// callers must not outlive it split apart.
SimulatedHardware make_simulated_hardware(const ssim::core::ScanConfig& scan_config,
                                          const ssim::core::NoiseConfig& noise_config,
                                          const WaferModel& wafer_model);

}  // namespace ssim::hw
