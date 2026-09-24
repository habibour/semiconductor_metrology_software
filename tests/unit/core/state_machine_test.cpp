#include <gtest/gtest.h>

#include "ssim/core/process_state_machine.hpp"

namespace ssim::core {
namespace {

TEST(ProcessStateMachine, StartsIdle) {
    ProcessStateMachine sm;
    EXPECT_EQ(sm.state(), ProcessState::kIdle);
}

TEST(ProcessStateMachine, StartMovesIdleToScanning) {
    ProcessStateMachine sm;
    auto result = sm.apply(ProcessTrigger::kStart);
    ASSERT_TRUE(result);
    EXPECT_EQ(result.value(), ProcessState::kScanning);
    EXPECT_EQ(sm.state(), ProcessState::kScanning);
}

TEST(ProcessStateMachine, StartRejectedWhenControlStateDenies) {
    ProcessStateMachine sm;
    TransitionGuards guards;
    guards.control_allows_start = false;

    auto result = sm.apply(ProcessTrigger::kStart, guards);

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, kReasonControlStateDenied);
    EXPECT_EQ(sm.state(), ProcessState::kIdle);  // unchanged
}

TEST(ProcessStateMachine, StartRejectedWhenAlarmActive) {
    ProcessStateMachine sm;
    TransitionGuards guards;
    guards.has_active_alarm = true;

    auto result = sm.apply(ProcessTrigger::kStart, guards);

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, kReasonActiveAlarm);
}

TEST(ProcessStateMachine, StartRejectedWhenWaferInvalid) {
    ProcessStateMachine sm;
    TransitionGuards guards;
    guards.wafer_valid = false;

    auto result = sm.apply(ProcessTrigger::kStart, guards);

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, kReasonInvalidWafer);
}

// UT-SM-1: every illegal transition is rejected with the right reason, and
// leaves state unchanged.
TEST(ProcessStateMachine, IllegalTriggerInEveryStateIsRejected) {
    struct Case {
        ProcessState state;
        ProcessTrigger illegal_trigger;
    };
    const Case cases[] = {
        {ProcessState::kIdle, ProcessTrigger::kAbort},
        {ProcessState::kScanning, ProcessTrigger::kStart},
        {ProcessState::kProcessing, ProcessTrigger::kStart},
        {ProcessState::kAlarm, ProcessTrigger::kStart},
        {ProcessState::kStopping, ProcessTrigger::kStart},
    };
    for (const auto& c : cases) {
        ProcessStateMachine sm(c.state);
        auto result = sm.apply(c.illegal_trigger);
        EXPECT_FALSE(result) << "state=" << to_string(c.state)
                             << " trigger=" << to_string(c.illegal_trigger);
        if (!result) {
            EXPECT_EQ(result.error().code, kReasonIllegalTransition);
        }
        EXPECT_EQ(sm.state(), c.state);
    }
}

TEST(ProcessStateMachine, ScanningLinesCompleteMovesToProcessing) {
    ProcessStateMachine sm(ProcessState::kScanning);
    auto result = sm.apply(ProcessTrigger::kLinesComplete);
    ASSERT_TRUE(result);
    EXPECT_EQ(sm.state(), ProcessState::kProcessing);
}

// PRD §6.5: Stop during Scanning is a same-state action (sets the
// stop-after-wafer flag), not a transition to Stopping.
TEST(ProcessStateMachine, StopDuringScanningStaysInScanningAndSetsFlag) {
    ProcessStateMachine sm(ProcessState::kScanning);
    auto result = sm.apply(ProcessTrigger::kStop);
    ASSERT_TRUE(result);
    EXPECT_EQ(sm.state(), ProcessState::kScanning);
    EXPECT_TRUE(sm.stop_after_wafer_requested());
}

TEST(ProcessStateMachine, AbortDuringScanningMovesToStopping) {
    ProcessStateMachine sm(ProcessState::kScanning);
    auto result = sm.apply(ProcessTrigger::kAbort);
    ASSERT_TRUE(result);
    EXPECT_EQ(sm.state(), ProcessState::kStopping);
}

TEST(ProcessStateMachine, FaultDuringScanningMovesToAlarm) {
    ProcessStateMachine sm(ProcessState::kScanning);
    auto result = sm.apply(ProcessTrigger::kFault);
    ASSERT_TRUE(result);
    EXPECT_EQ(sm.state(), ProcessState::kAlarm);
}

TEST(ProcessStateMachine, ResultReadyWithNoMoreWafersMovesToIdle) {
    ProcessStateMachine sm(ProcessState::kProcessing);
    auto result = sm.apply(ProcessTrigger::kResultReady);
    ASSERT_TRUE(result);
    EXPECT_EQ(sm.state(), ProcessState::kIdle);
}

TEST(ProcessStateMachine, ResultReadyWithMoreWafersMovesToScanning) {
    ProcessStateMachine sm(ProcessState::kProcessing);
    TransitionGuards guards;
    guards.more_wafers_pending = true;
    auto result = sm.apply(ProcessTrigger::kResultReady, guards);
    ASSERT_TRUE(result);
    EXPECT_EQ(sm.state(), ProcessState::kScanning);
}

TEST(ProcessStateMachine, ResultReadyIgnoresMoreWafersWhenStopWasRequested) {
    ProcessStateMachine sm(ProcessState::kScanning);
    ASSERT_TRUE(sm.apply(ProcessTrigger::kStop));  // sets stop_after_wafer_
    ASSERT_TRUE(sm.apply(ProcessTrigger::kLinesComplete));

    TransitionGuards guards;
    guards.more_wafers_pending = true;
    auto result = sm.apply(ProcessTrigger::kResultReady, guards);

    ASSERT_TRUE(result);
    EXPECT_EQ(sm.state(), ProcessState::kIdle);
}

TEST(ProcessStateMachine, ClearAlarmMovesAlarmToIdle) {
    ProcessStateMachine sm(ProcessState::kAlarm);
    auto result = sm.apply(ProcessTrigger::kClearAlarm);
    ASSERT_TRUE(result);
    EXPECT_EQ(sm.state(), ProcessState::kIdle);
}

TEST(ProcessStateMachine, ClearAlarmRejectedWhenCauseNotCleared) {
    ProcessStateMachine sm(ProcessState::kAlarm);
    TransitionGuards guards;
    guards.alarm_cause_cleared = false;

    auto result = sm.apply(ProcessTrigger::kClearAlarm, guards);

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, kReasonAlarmNotCleared);
    EXPECT_EQ(sm.state(), ProcessState::kAlarm);
}

TEST(ProcessStateMachine, StageStoppedMovesStoppingToIdle) {
    ProcessStateMachine sm(ProcessState::kStopping);
    auto result = sm.apply(ProcessTrigger::kStageStopped);
    ASSERT_TRUE(result);
    EXPECT_EQ(sm.state(), ProcessState::kIdle);
}

}  // namespace
}  // namespace ssim::core
