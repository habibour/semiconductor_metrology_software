#pragma once

// Thread-safety: Thread-safe. The command methods block the calling thread
// until the controller thread has applied the command (they forward to
// Controller::submit_command), so a GUI must call them off its own thread.
// snapshot() never blocks on the controller: it reads atomics plus one small
// mutex that guards the cached last result.
//
// PRD §6.6 (Facade): the few operations the Qt panel, and later the CLI and
// the SECS/GEM handlers, need. Callers never reach into the Controller.
// The snapshot is kept current by EventBus subscriptions made in the
// constructor and removed in the destructor; the handlers run on whichever
// thread publishes, and take no lock while calling out (rule C3).

#include <atomic>
#include <mutex>
#include <optional>
#include <string>

#include "ssim/core/alarms.hpp"
#include "ssim/core/command.hpp"
#include "ssim/core/controller.hpp"
#include "ssim/core/event_bus.hpp"
#include "ssim/core/events.hpp"
#include "ssim/core/process_state_machine.hpp"
#include "ssim/core/result.hpp"

namespace ssim::core {

struct MachineSnapshot {
    ProcessState process_state = ProcessState::kIdle;
    ControlMode control_mode = ControlMode::kOnlineLocal;
    bool alarm_active = false;
    double progress_percent = 0.0;  // 0..100, reset to 0 when a scan starts
    std::string wafer_id;
    std::optional<WaferResultReady> last_result;
};

class MachineApi {
public:
    MachineApi(Controller& controller, EventBus& bus);
    ~MachineApi();

    MachineApi(const MachineApi&) = delete;
    MachineApi& operator=(const MachineApi&) = delete;

    // Each returns the resulting process state, or an Error carrying one of
    // process_state_machine.hpp's kReason* codes. Every call gets a fresh
    // correlation id (FR-MC-2, NFR-OBS-1).
    [[nodiscard]] Result<ProcessState> start(std::string wafer_id, int slot, CommandSource source);
    [[nodiscard]] Result<ProcessState> stop(CommandSource source);
    [[nodiscard]] Result<ProcessState> abort(CommandSource source);
    [[nodiscard]] Result<ProcessState> clear_alarm(CommandSource source);
    [[nodiscard]] Result<ProcessState> set_control_mode(ControlMode mode, CommandSource source);

    [[nodiscard]] MachineSnapshot snapshot() const;

private:
    Result<ProcessState> submit(CommandPayload payload, CommandSource source);

    Controller& controller_;
    EventBus& bus_;

    std::atomic<CorrelationId> next_correlation_id_{1};
    std::atomic<double> progress_percent_{0.0};

    mutable std::mutex result_mutex_;
    std::optional<WaferResultReady> last_result_;

    EventBus::SubscriptionId progress_sub_ = 0;
    EventBus::SubscriptionId started_sub_ = 0;
    EventBus::SubscriptionId result_sub_ = 0;
};

}  // namespace ssim::core
