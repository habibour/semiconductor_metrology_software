#pragma once

// Thread-safety: Controller is safe to call from any thread. Every method
// that changes machine state (submit_command, notify_scan_complete,
// notify_processing_result, notify_fault) posts the mutation onto the
// controller's own thread and blocks (condition-variable wait inside
// BoundedQueue, never a sleep) until that thread has applied it and
// published any resulting events — this is how concurrency rule C1 (the
// controller thread is the only writer of machine state) holds even though
// callers on other threads get a synchronous-looking API. The read-only
// snapshot accessors (state(), control_mode(), has_active_alarm()) are
// plain atomics, safe to poll from any thread without blocking.
//
// FR-MC-1/2/4/6, PRD §6.5: owns the process state machine and the single
// command queue; every command carries its source and is checked against
// the control-state rules before being applied. Does not itself run the
// scan or the analysis pipeline — it depends only on IScanDriver (dependency
// injection, PRD §6.6) so it stays in ssim_core without depending on
// ssim_hw or ssim_analysis (PRD §6.2). The composition root (equipment_cli
// today) owns the concrete ScanThread and the analysis pipeline, and reports
// their completion back through notify_scan_complete/notify_processing_result.

#include <atomic>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include "ssim/core/alarms.hpp"
#include "ssim/core/command.hpp"
#include "ssim/core/event_bus.hpp"
#include "ssim/core/iscan_driver.hpp"
#include "ssim/core/process_state_machine.hpp"
#include "ssim/core/queue.hpp"
#include "ssim/core/result.hpp"

namespace ssim::core {

class Controller {
public:
    Controller(EventBus& bus, IScanDriver& scan_driver, AlarmManager& alarms);
    ~Controller();

    Controller(const Controller&) = delete;
    Controller& operator=(const Controller&) = delete;

    // Starts the controller thread. Must be called once before any of the
    // notify_*/submit_command methods.
    void start();

    // FR-MC-6 shutdown protocol: aborts an in-flight scan, joins it, then
    // closes the inbox and joins the controller thread. Safe to call more
    // than once and even if start() was never called.
    void stop();

    // FR-MC-2. Returns the resulting process state, or an Error with one of
    // process_state_machine.hpp's kReason* codes if the control-state rules
    // or the state machine itself rejected it.
    [[nodiscard]] Result<ProcessState> submit_command(Command command);

    // PRD §6.5 "All lines complete": posted by whichever thread owns the
    // scan driver once it has finished (today, the composition root, after
    // its own subscription to ScanLinesComplete or driver-specific signal;
    // see src/app_cli/main.cpp).
    void notify_scan_complete(std::string wafer_id);

    // Posted by the composition root once it has finished running the
    // ssim_analysis pipeline for the wafer currently being processed.
    // raised_alarm takes precedence over out_of_spec (PRD §6.5: a quality
    // gate raises an alarm and reports no number; out-of-spec is a flag on
    // an otherwise-valid result). more_wafers_pending is always false until
    // the Day 3 cassette loop exists.
    // raised_alarm_id set to a real AlarmId (e.g. kFitQualityPoor,
    // kStressImplausible) drives Processing -> Alarm instead of
    // Processing -> Idle/Scanning; nullopt means the result is usable.
    void notify_processing_result(bool out_of_spec, double stress_mpa,
                                  std::optional<AlarmId> raised_alarm_id, std::string alarm_reason,
                                  bool more_wafers_pending);

    // A fault occurring mid-scan (FR-ALM-3 watchdogs, or a hardware fault
    // severe enough to abort the run). Legal only from Scanning or
    // Processing; the FSM rejects it otherwise, and the alarm is still
    // recorded/published via AlarmManager regardless.
    void notify_fault(AlarmId id, std::string reason);

    // PRD §6.5 "Stage stopped": posted by the scan driver's own background
    // thread right before it exits, after an abort. Legal only from
    // Stopping; a stray call while not in Stopping (e.g. a raw teardown
    // that never went through AbortCommand first) is silently rejected by
    // the FSM rather than corrupting state.
    void notify_stage_stopped();

    ProcessState state() const { return state_.load(); }
    ControlMode control_mode() const { return control_mode_.load(); }
    bool has_active_alarm() const { return has_active_alarm_.load(); }
    std::string current_wafer_id() const;

private:
    void run();
    Result<ProcessState> handle_command(const Command& command);
    Result<ProcessState> transition(ProcessTrigger trigger, const TransitionGuards& guards);

    // Runs fn on the controller thread and blocks the caller until it
    // completes. The returned Result is whatever fn produced.
    Result<ProcessState> run_on_controller_thread(std::function<Result<ProcessState>()> fn);

    EventBus& bus_;
    IScanDriver& scan_driver_;
    AlarmManager& alarms_;

    BoundedQueue<std::function<void()>> inbox_;
    std::thread thread_;

    ProcessStateMachine fsm_;
    std::atomic<ProcessState> state_{ProcessState::kIdle};
    std::atomic<ControlMode> control_mode_{ControlMode::kOnlineLocal};
    std::atomic<bool> has_active_alarm_{false};

    mutable std::mutex wafer_id_mutex_;
    std::string current_wafer_id_;
};

}  // namespace ssim::core
