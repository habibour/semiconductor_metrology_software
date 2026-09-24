#include "ssim/core/ui_rules.hpp"

#include <gtest/gtest.h>

namespace ssim::core {
namespace {

constexpr ProcessState kAllStates[] = {ProcessState::kIdle, ProcessState::kScanning,
                                       ProcessState::kProcessing, ProcessState::kAlarm,
                                       ProcessState::kStopping};
constexpr ControlMode kAllModes[] = {ControlMode::kOffline, ControlMode::kOnlineLocal,
                                     ControlMode::kOnlineRemote};

TEST(UiRules, StartOnlyInIdleWithoutAlarmAndNotRemote) {
    for (ProcessState state : kAllStates) {
        for (ControlMode mode : kAllModes) {
            for (bool alarm : {false, true}) {
                const bool expected =
                    state == ProcessState::kIdle && !alarm && mode != ControlMode::kOnlineRemote;
                EXPECT_EQ(rules_for(state, mode, alarm).can_start, expected)
                    << to_string(state) << " / " << to_string(mode) << " / alarm=" << alarm;
            }
        }
    }
}

TEST(UiRules, StopOnlyWhileScanning) {
    for (ProcessState state : kAllStates) {
        for (ControlMode mode : kAllModes) {
            EXPECT_EQ(rules_for(state, mode, false).can_stop, state == ProcessState::kScanning);
        }
    }
}

// PRD §6.5: Stop and Abort stay accepted in Online-Remote for safety.
TEST(UiRules, AbortInScanningAndProcessingInEveryMode) {
    for (ProcessState state : kAllStates) {
        for (ControlMode mode : kAllModes) {
            const bool expected =
                state == ProcessState::kScanning || state == ProcessState::kProcessing;
            EXPECT_EQ(rules_for(state, mode, false).can_abort, expected)
                << to_string(state) << " / " << to_string(mode);
        }
    }
}

TEST(UiRules, ClearAlarmOnlyInAlarmState) {
    for (ProcessState state : kAllStates) {
        for (ControlMode mode : kAllModes) {
            EXPECT_EQ(rules_for(state, mode, true).can_clear_alarm, state == ProcessState::kAlarm);
        }
    }
}

TEST(UiRules, ControlModeChangeableOnlyWhenIdleOrAlarm) {
    for (ProcessState state : kAllStates) {
        const bool expected = state == ProcessState::kIdle || state == ProcessState::kAlarm;
        EXPECT_EQ(rules_for(state, ControlMode::kOnlineLocal, false).can_change_mode, expected);
    }
}

}  // namespace
}  // namespace ssim::core
