#include "ssim/core/process_state_machine.hpp"

namespace ssim::core {

const char* to_string(ProcessState state) {
    switch (state) {
        case ProcessState::kIdle:
            return "Idle";
        case ProcessState::kScanning:
            return "Scanning";
        case ProcessState::kProcessing:
            return "Processing";
        case ProcessState::kAlarm:
            return "Alarm";
        case ProcessState::kStopping:
            return "Stopping";
    }
    return "Unknown";
}

const char* to_string(ProcessTrigger trigger) {
    switch (trigger) {
        case ProcessTrigger::kStart:
            return "Start";
        case ProcessTrigger::kLinesComplete:
            return "LinesComplete";
        case ProcessTrigger::kStop:
            return "Stop";
        case ProcessTrigger::kAbort:
            return "Abort";
        case ProcessTrigger::kFault:
            return "Fault";
        case ProcessTrigger::kResultReady:
            return "ResultReady";
        case ProcessTrigger::kClearAlarm:
            return "ClearAlarm";
        case ProcessTrigger::kStageStopped:
            return "StageStopped";
    }
    return "Unknown";
}

namespace {
Result<ProcessState> reject(int reason, const char* message) {
    return Result<ProcessState>::err(Error{reason, message});
}
}  // namespace

Result<ProcessState> ProcessStateMachine::apply(ProcessTrigger trigger,
                                                const TransitionGuards& guards) {
    switch (state_) {
        case ProcessState::kIdle: {
            if (trigger != ProcessTrigger::kStart) {
                return reject(kReasonIllegalTransition, "Idle only accepts Start");
            }
            if (!guards.control_allows_start) {
                return reject(kReasonControlStateDenied, "Start denied by control state");
            }
            if (guards.has_active_alarm) {
                return reject(kReasonActiveAlarm, "Start denied: an alarm is active");
            }
            if (!guards.wafer_valid) {
                return reject(kReasonInvalidWafer, "Start denied: no valid wafer or cassette");
            }
            stop_after_wafer_ = false;
            state_ = ProcessState::kScanning;
            return Result<ProcessState>::ok(state_);
        }
        case ProcessState::kScanning: {
            switch (trigger) {
                case ProcessTrigger::kLinesComplete:
                    state_ = ProcessState::kProcessing;
                    return Result<ProcessState>::ok(state_);
                case ProcessTrigger::kStop:
                    stop_after_wafer_ = true;
                    return Result<ProcessState>::ok(state_);  // same-state action
                case ProcessTrigger::kAbort:
                    state_ = ProcessState::kStopping;
                    return Result<ProcessState>::ok(state_);
                case ProcessTrigger::kFault:
                    state_ = ProcessState::kAlarm;
                    return Result<ProcessState>::ok(state_);
                default:
                    return reject(kReasonIllegalTransition,
                                  "Scanning only accepts LinesComplete, Stop, Abort, Fault");
            }
        }
        case ProcessState::kProcessing: {
            switch (trigger) {
                case ProcessTrigger::kResultReady:
                    state_ = (guards.more_wafers_pending && !stop_after_wafer_)
                                 ? ProcessState::kScanning
                                 : ProcessState::kIdle;
                    if (state_ == ProcessState::kIdle) {
                        stop_after_wafer_ = false;
                    }
                    return Result<ProcessState>::ok(state_);
                case ProcessTrigger::kFault:
                    state_ = ProcessState::kAlarm;
                    return Result<ProcessState>::ok(state_);
                case ProcessTrigger::kAbort:
                    state_ = ProcessState::kStopping;
                    return Result<ProcessState>::ok(state_);
                default:
                    return reject(kReasonIllegalTransition,
                                  "Processing only accepts ResultReady, Fault, Abort");
            }
        }
        case ProcessState::kAlarm: {
            if (trigger != ProcessTrigger::kClearAlarm) {
                return reject(kReasonIllegalTransition, "Alarm only accepts ClearAlarm");
            }
            if (!guards.alarm_cause_cleared) {
                return reject(kReasonAlarmNotCleared, "ClearAlarm denied: cause still present");
            }
            state_ = ProcessState::kIdle;
            return Result<ProcessState>::ok(state_);
        }
        case ProcessState::kStopping: {
            if (trigger != ProcessTrigger::kStageStopped) {
                return reject(kReasonIllegalTransition, "Stopping only accepts StageStopped");
            }
            state_ = ProcessState::kIdle;
            stop_after_wafer_ = false;
            return Result<ProcessState>::ok(state_);
        }
    }
    return reject(kReasonIllegalTransition, "unreachable state");
}

}  // namespace ssim::core
