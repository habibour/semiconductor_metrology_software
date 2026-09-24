#include "ssim/core/ui_rules.hpp"

namespace ssim::core {

ActionRules rules_for(ProcessState state, ControlMode mode, bool alarm_active) {
    ActionRules rules;
    // Online-Remote: the host is in charge, so the operator cannot Start.
    rules.can_start =
        state == ProcessState::kIdle && !alarm_active && mode != ControlMode::kOnlineRemote;
    rules.can_stop = state == ProcessState::kScanning;
    // Abort is allowed in every control mode, for safety (PRD §6.5).
    rules.can_abort = state == ProcessState::kScanning || state == ProcessState::kProcessing;
    rules.can_clear_alarm = state == ProcessState::kAlarm;
    rules.can_change_mode = state == ProcessState::kIdle || state == ProcessState::kAlarm;
    return rules;
}

}  // namespace ssim::core
