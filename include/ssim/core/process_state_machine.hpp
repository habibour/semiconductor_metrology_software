#pragma once

// Thread-safety: Not thread-safe. Concurrency rule C1 (CLAUDE.md §6.2): the
// controller thread is the only writer of machine state, so this class is
// owned and driven exclusively by the controller; no internal locking is
// needed or provided.
//
// FR-MC-1 / PRD §6.5: the process state machine table verbatim. apply()
// rejects any transition not in that table by leaving the state unchanged
// and returning a stable reason code (never throws) — CLAUDE.md §6.3,
// expected failures are values.

#include "ssim/core/result.hpp"

namespace ssim::core {

enum class ProcessState { kIdle, kScanning, kProcessing, kAlarm, kStopping };

const char* to_string(ProcessState state);

enum class ProcessTrigger {
    kStart,
    kLinesComplete,
    kStop,
    kAbort,
    kFault,
    kResultReady,
    kClearAlarm,
    kStageStopped,
};

const char* to_string(ProcessTrigger trigger);

// Reason codes for a rejected transition (UT-SM-1 asserts on these, so they
// must stay stable). Distinct from Config's exit-code Errors: these are
// process-control reasons, not process exit codes.
constexpr int kReasonIllegalTransition = 100;   // trigger not valid in this state
constexpr int kReasonControlStateDenied = 101;  // Start: command source not allowed to start
constexpr int kReasonActiveAlarm = 102;         // Start: an alarm is already active
constexpr int kReasonInvalidWafer = 103;        // Start: no valid wafer or cassette queued
constexpr int kReasonAlarmNotCleared = 104;     // ClearAlarm: the alarm cause is still present

// Guard inputs for kStart and kClearAlarm and the kResultReady branch (PRD
// §6.5's "Guard" column). Every field defaults to the value that lets the
// happy path through, so a test only sets the guard it means to exercise.
struct TransitionGuards {
    bool control_allows_start = true;
    bool has_active_alarm = false;
    bool wafer_valid = true;
    bool more_wafers_pending = false;
    bool alarm_cause_cleared = true;
};

class ProcessStateMachine {
public:
    explicit ProcessStateMachine(ProcessState initial = ProcessState::kIdle) : state_(initial) {}

    ProcessState state() const { return state_; }

    // True from the moment a kStop trigger lands while Scanning until the
    // run leaves Scanning (PRD §6.5: "Set stop-after-wafer flag" is a
    // same-state action, not a transition, so it cannot be observed from
    // state() alone).
    bool stop_after_wafer_requested() const { return stop_after_wafer_; }

    // On success, updates state() (kStop while Scanning is a same-state
    // success: it returns ok(kScanning) and sets stop_after_wafer_).
    // On failure, state() is unchanged and the Error carries one of the
    // kReason* codes above plus a human-readable message.
    [[nodiscard]] Result<ProcessState> apply(ProcessTrigger trigger,
                                             const TransitionGuards& guards = {});

private:
    ProcessState state_;
    bool stop_after_wafer_ = false;
};

}  // namespace ssim::core
