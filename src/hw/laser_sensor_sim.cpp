#include "ssim/hw/laser_sensor_sim.hpp"

namespace ssim::hw {

LaserSensorSim::LaserSensorSim(const IStage& stage, const WaferModel& wafer_model, double sigma_m,
                               std::uint64_t seed)
    : stage_(stage), wafer_model_(wafer_model), rng_(seed), noise_(0.0, sigma_m) {}

double LaserSensorSim::read_height_m() {
    const double ideal =
        wafer_model_.height_m(stage_.current_angle_rad(), stage_.current_position_m());
    return ideal + noise_(rng_);
}

}  // namespace ssim::hw
