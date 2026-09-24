#include "ssim/core/machine_api.hpp"

#include <gtest/gtest.h>

#include <atomic>

#include "ssim/core/alarms.hpp"
#include "ssim/core/controller.hpp"
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
    void join() override {}

    std::string started_wafer_id;
    std::atomic<int> start_count{0};
    std::atomic<bool> abort_requested{false};
};

struct Fixture {
    EventBus bus;
    FakeScanDriver driver;
    AlarmManager alarms{bus};
    Controller controller{bus, driver, alarms};
    MachineApi api{controller, bus};

    Fixture() { controller.start(); }
    ~Fixture() { controller.stop(); }
};

TEST(MachineApi, StartForwardsToTheDriverAndReportsScanning) {
    Fixture f;
    auto result = f.api.start("W007", 3, CommandSource::kUi);
    ASSERT_TRUE(result);
    EXPECT_EQ(result.value(), ProcessState::kScanning);
    EXPECT_EQ(f.driver.started_wafer_id, "W007");

    const MachineSnapshot snap = f.api.snapshot();
    EXPECT_EQ(snap.process_state, ProcessState::kScanning);
    EXPECT_EQ(snap.wafer_id, "W007");
}

// FR-MC-4 for the UI source: Start is refused in Online-Remote, but
// Stop and Abort stay accepted (PRD §6.5).
TEST(MachineApi, UiStartRejectedInRemoteButAbortStillAccepted) {
    Fixture f;
    ASSERT_TRUE(f.api.set_control_mode(ControlMode::kOnlineRemote, CommandSource::kUi));
    EXPECT_EQ(f.api.snapshot().control_mode, ControlMode::kOnlineRemote);

    auto start = f.api.start("W001", 1, CommandSource::kUi);
    ASSERT_FALSE(start);
    EXPECT_EQ(start.error().code, kReasonControlStateDenied);
    EXPECT_EQ(f.driver.start_count.load(), 0);

    ASSERT_TRUE(f.api.set_control_mode(ControlMode::kOnlineLocal, CommandSource::kUi));
    ASSERT_TRUE(f.api.start("W001", 1, CommandSource::kUi));
    ASSERT_TRUE(f.api.set_control_mode(ControlMode::kOnlineRemote, CommandSource::kUi));
    EXPECT_TRUE(f.api.abort(CommandSource::kUi));
    EXPECT_TRUE(f.driver.abort_requested.load());
}

TEST(MachineApi, ProgressAndResultEventsUpdateTheSnapshot) {
    Fixture f;
    ASSERT_TRUE(f.api.start("W001", 1, CommandSource::kUi));

    f.bus.publish(ScanProgress{"W001", 42.0});
    EXPECT_DOUBLE_EQ(f.api.snapshot().progress_percent, 42.0);

    WaferResultReady result;
    result.wafer_id = "W001";
    result.stress_mpa = -179.4;
    f.bus.publish(result);
    const MachineSnapshot snap = f.api.snapshot();
    ASSERT_TRUE(snap.last_result.has_value());
    EXPECT_DOUBLE_EQ(snap.last_result->stress_mpa, -179.4);
}

TEST(MachineApi, ScanStartedResetsProgress) {
    Fixture f;
    f.bus.publish(ScanProgress{"W001", 90.0});
    f.bus.publish(ScanStarted{"W002", 1});
    EXPECT_DOUBLE_EQ(f.api.snapshot().progress_percent, 0.0);
}

TEST(MachineApi, IllegalCommandReturnsTheReasonCode) {
    Fixture f;
    auto result = f.api.stop(CommandSource::kUi);  // Stop while Idle
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, kReasonIllegalTransition);
}

}  // namespace
}  // namespace ssim::core
