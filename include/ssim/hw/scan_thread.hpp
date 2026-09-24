#pragma once

// Thread-safety: owns exactly one thread ("scan"). start()/request_abort()/
// join() are safe to call from any thread. Not safe to call start() again
// before join() has returned for the previous run (ssim::core::IScanDriver's
// contract). Holds non-owning references to the stage, laser and wafer
// model it drives — the composition root must keep them alive at least as
// long as this object (same lifetime discipline as hardware_factory's
// SimulatedHardware).
//
// FR-SCN-1..4: implements ssim::core::IScanDriver so the controller can
// drive it without ssim_core depending on ssim_hw (PRD §6.2). Owns a
// FaultInjector per matching FaultConfig entry for the wafer currently
// being scanned, built fresh in start() because the matching wafer id is
// only known then.

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#include "ssim/core/alarms.hpp"
#include "ssim/core/config.hpp"
#include "ssim/core/event_bus.hpp"
#include "ssim/core/iscan_driver.hpp"
#include "ssim/core/queue.hpp"
#include "ssim/core/scan_types.hpp"
#include "ssim/hw/fault_injector.hpp"
#include "ssim/hw/ilaser_sensor.hpp"
#include "ssim/hw/istage.hpp"
#include "ssim/hw/wafer_model.hpp"

namespace ssim::hw {

class ScanThread final : public ssim::core::IScanDriver {
public:
    ScanThread(IStage& stage, ILaserSensor& laser, const WaferModel& wafer_model,
               ssim::core::ScanConfig scan_config,
               std::vector<ssim::core::FaultConfig> fault_configs,
               ssim::core::BoundedQueue<ssim::core::SampleBlock>& sample_queue,
               ssim::core::EventBus& bus, ssim::core::AlarmManager& alarms,
               std::function<void(std::string)> on_lines_complete,
               std::function<void()> on_stopped);
    ~ScanThread() override;

    ScanThread(const ScanThread&) = delete;
    ScanThread& operator=(const ScanThread&) = delete;

    void start(std::string wafer_id) override;
    void request_abort() override;
    void join() override;

private:
    void run(std::string wafer_id);

    IStage& stage_;
    ILaserSensor& laser_;
    const WaferModel& wafer_model_;
    ssim::core::ScanConfig scan_config_;
    std::vector<ssim::core::FaultConfig> fault_configs_;
    ssim::core::BoundedQueue<ssim::core::SampleBlock>& sample_queue_;
    ssim::core::EventBus& bus_;
    ssim::core::AlarmManager& alarms_;
    std::function<void(std::string)> on_lines_complete_;
    std::function<void()> on_stopped_;

    std::atomic<bool> abort_requested_{false};
    std::thread thread_;
};

}  // namespace ssim::hw
