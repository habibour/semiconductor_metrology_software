#pragma once

// Thread-safety: Thread-safe (a pure function of its arguments).
//
// FR-UI-1 / PRD §6.5: which operator buttons the panel may enable for a
// given machine snapshot. Kept in ssim_core, free of Qt, so the rule table
// is unit-tested on every CI system instead of only being eyeballed in the
// GUI. The controller stays the authority: it still rejects an illegal
// command with a reason code, these rules only stop the panel from offering
// one.

#include "ssim/core/command.hpp"
#include "ssim/core/process_state_machine.hpp"

namespace ssim::core {

struct ActionRules {
    bool can_start = false;
    bool can_stop = false;
    bool can_abort = false;
    bool can_clear_alarm = false;
    bool can_change_mode = false;
};

[[nodiscard]] ActionRules rules_for(ProcessState state, ControlMode mode, bool alarm_active);

}  // namespace ssim::core
