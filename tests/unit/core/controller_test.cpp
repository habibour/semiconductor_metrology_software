#include "ssim/core/controller.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>

#include "ssim/core/alarms.hpp"
#include "ssim/core/events.hpp"

namespace ssim::core {
namespace {

class FakeScanDriver : public IScanDriver {
public:
    void start(std::string wafer_id) override {
        started_wafer_id = std::move(wafer_id);
        ++start_count;
    }
    void request_abort() override { abort_requested = true; }
    void join() override { ++join_count; }

    std::string started_wafer_id;
    std::atomic<int> start_count{0};
    std::atomic<bool> abort_requested{false};
    std::atomic<int> join_count{0};
};

struct Fixture {
    EventBus bus;
    FakeScanDriver driver;
    AlarmManager alarms{bus};
    Controller controller{bus, driver, alarms};

    Fixture() { controller.start(); }
    ~Fixture() { controller.stop(); }
};

Command start(CommandSource source, std::string wafer_id = "W001") {
    return Command{StartCommand{std::move(wafer_id), 1}, source, 1};
}

TEST(Controller, StartFromUiSucceedsInDefaultOnlineLocalMode) {
    Fixture f;
    auto result = f.controller.submit_command(start(CommandSource::kUi));
    ASSERT_TRUE(result);
    EXPECT_EQ(f.controller.state(), ProcessState::kScanning);
    EXPECT_EQ(f.driver.started_wafer_id, "W001");
}

// UT-CTRL-1 / FR-MC-4: host (SECS/GEM) commands are rejected while the
// control mode is Offline/Online-Local.
TEST(Controller, StartFromSecsGemRejectedInOnlineLocalMode) {
    Fixture f;
    auto result = f.controller.submit_command(start(CommandSource::kSecsGem));
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, kReasonControlStateDenied);
    EXPECT_EQ(f.controller.state(), ProcessState::kIdle);
    EXPECT_EQ(f.driver.start_count.load(), 0);
}

// Once Online-Remote, the host may start but the operator (UI/CLI) may not.
TEST(Controller, OnlineRemoteFlipsWhichSourceMayStart) {
    Fixture f;
    auto mode_result = f.controller.submit_command(
        Command{SetControlModeCommand{ControlMode::kOnlineRemote}, CommandSource::kUi, 1});
    ASSERT_TRUE(mode_result);
    EXPECT_EQ(f.controller.control_mode(), ControlMode::kOnlineRemote);

    auto ui_start = f.controller.submit_command(start(CommandSource::kCli));
    EXPECT_FALSE(ui_start);
    EXPECT_EQ(ui_start.error().code, kReasonControlStateDenied);

    auto host_start = f.controller.submit_command(start(CommandSource::kSecsGem));
    EXPECT_TRUE(host_start);
    EXPECT_EQ(f.controller.state(), ProcessState::kScanning);
}

// FR-MC-4: Stop/Abort are safety commands, accepted regardless of source or
// control mode, even while Online-Remote.
TEST(Controller, StopAndAbortAlwaysAcceptedRegardlessOfSourceOrMode) {
    Fixture f;
    ASSERT_TRUE(f.controller.submit_command(
        Command{SetControlModeCommand{ControlMode::kOnlineRemote}, CommandSource::kUi, 1}));
    ASSERT_TRUE(f.controller.submit_command(start(CommandSource::kSecsGem)));

    auto abort_result = f.controller.submit_command(Command{AbortCommand{}, CommandSource::kCli, 2});
    ASSERT_TRUE(abort_result);
    EXPECT_EQ(f.controller.state(), ProcessState::kStopping);
    EXPECT_TRUE(f.driver.abort_requested.load());
}

TEST(Controller, SecsGemCannotSetControlMode) {
    Fixture f;
    auto result = f.controller.submit_command(
        Command{SetControlModeCommand{ControlMode::kOffline}, CommandSource::kSecsGem, 1});
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, kReasonControlStateDenied);
}

TEST(Controller, NotifyScanCompleteDrivesScanningToProcessing) {
    Fixture f;
    ASSERT_TRUE(f.controller.submit_command(start(CommandSource::kUi)));
    f.controller.notify_scan_complete("W001");
    EXPECT_EQ(f.controller.state(), ProcessState::kProcessing);
}

TEST(Controller, NotifyProcessingResultDrivesProcessingToIdleAndPublishesScanComplete) {
    Fixture f;
    int complete_count = 0;
    f.bus.subscribe<ScanComplete>([&](const ScanComplete&) { ++complete_count; });

    ASSERT_TRUE(f.controller.submit_command(start(CommandSource::kUi)));
    f.controller.notify_scan_complete("W001");
    f.controller.notify_processing_result(/*out_of_spec=*/false, /*stress_mpa=*/-180.0,
                                          /*raised_alarm_id=*/std::nullopt, "", false);

    EXPECT_EQ(f.controller.state(), ProcessState::kIdle);
    EXPECT_EQ(complete_count, 1);
}

TEST(Controller, NotifyProcessingResultWithAlarmDrivesProcessingToAlarmAndBlocksNextStart) {
    Fixture f;
    ASSERT_TRUE(f.controller.submit_command(start(CommandSource::kUi)));
    f.controller.notify_scan_complete("W001");
    f.controller.notify_processing_result(false, 0.0, AlarmId::kFitQualityPoor, "rms too high",
                                          false);

    EXPECT_EQ(f.controller.state(), ProcessState::kAlarm);
    EXPECT_TRUE(f.controller.has_active_alarm());

    // Rejected because the FSM is in Alarm (which only accepts ClearAlarm);
    // ProcessStateMachine's separate has_active_alarm guard (state_machine_
    // test.cpp's StartRejectedWhenAlarmActive) covers the Idle-with-a-
    // stale-alarm-flag case directly — Controller never produces that
    // combination since it always clears has_active_alarm_ in lockstep with
    // leaving Alarm.
    auto blocked = f.controller.submit_command(start(CommandSource::kUi, "W002"));
    EXPECT_FALSE(blocked);
    EXPECT_EQ(blocked.error().code, kReasonIllegalTransition);

    auto cleared =
        f.controller.submit_command(Command{ClearAlarmCommand{}, CommandSource::kUi, 3});
    ASSERT_TRUE(cleared);
    EXPECT_EQ(f.controller.state(), ProcessState::kIdle);
    EXPECT_FALSE(f.controller.has_active_alarm());
}

// FT-SHUTDOWN-1: stop() while mid-scan aborts the driver, joins it, and the
// controller thread, all well within the 2 s budget (rtf isn't in play here
// since FakeScanDriver never sleeps; this asserts the *protocol* runs
// quickly, not real hardware timing).
TEST(Controller, StopDuringScanAbortsAndJoinsWithinBudget) {
    Fixture f;
    ASSERT_TRUE(f.controller.submit_command(start(CommandSource::kUi)));

    const auto begin = std::chrono::steady_clock::now();
    f.controller.stop();
    const auto elapsed = std::chrono::steady_clock::now() - begin;

    EXPECT_TRUE(f.driver.abort_requested.load());
    EXPECT_EQ(f.driver.join_count.load(), 1);
    EXPECT_LT(elapsed, std::chrono::seconds(2));
}

}  // namespace
}  // namespace ssim::core
