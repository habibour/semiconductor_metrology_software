#include "ssim/machine/machine_runtime.hpp"

#include <gtest/gtest.h>
#include <unistd.h>

#include <chrono>
#include <cmath>
#include <condition_variable>
#include <filesystem>
#include <functional>
#include <mutex>
#include <vector>

#include "ssim/core/events.hpp"
#include "ssim/machine/machine_events.hpp"

namespace ssim::machine {
namespace {

using ssim::core::CommandSource;
using ssim::core::ProcessState;

constexpr auto kTimeout = std::chrono::seconds(30);

// Collects the events the tests wait on. Waiting is bounded by a timeout
// only as a safety net; the tests themselves never sleep.
class Recorder {
public:
    explicit Recorder(ssim::core::EventBus& bus) : bus_(bus) {
        ids_.push_back(bus.subscribe<ssim::core::WaferResultReady>(
            [this](const ssim::core::WaferResultReady& e) {
                std::lock_guard lock(mutex_);
                results.push_back(e);
                cv_.notify_all();
            }));
        ids_.push_back(bus.subscribe<WaferMapReady>([this](const WaferMapReady& e) {
            std::lock_guard lock(mutex_);
            maps.push_back(e);
            cv_.notify_all();
        }));
        ids_.push_back(bus.subscribe<WaferOutputsWritten>([this](const WaferOutputsWritten& e) {
            std::lock_guard lock(mutex_);
            outputs.push_back(e);
            cv_.notify_all();
        }));
        ids_.push_back(bus.subscribe<ssim::core::AlarmSet>([this](const ssim::core::AlarmSet& e) {
            std::lock_guard lock(mutex_);
            alarms.push_back(e);
            cv_.notify_all();
        }));
        ids_.push_back(
            bus.subscribe<ssim::core::StateChanged>([this](const ssim::core::StateChanged& e) {
                std::lock_guard lock(mutex_);
                states.push_back(e.to);
                cv_.notify_all();
            }));
    }

    // The runtime keeps publishing while it shuts down, after this object
    // (declared later in each test) is gone, so detach from the bus first.
    ~Recorder() {
        for (auto id : ids_) {
            bus_.unsubscribe(id);
        }
    }

    bool wait_until(const std::function<bool()>& predicate) {
        std::unique_lock lock(mutex_);
        return cv_.wait_for(lock, kTimeout, predicate);
    }

    bool wait_for_state_count(ProcessState state, std::size_t count) {
        return wait_until([&] {
            std::size_t n = 0;
            for (auto s : states) {
                if (s == state) ++n;
            }
            return n >= count;
        });
    }

    std::vector<ssim::core::WaferResultReady> results;
    std::vector<WaferMapReady> maps;
    std::vector<WaferOutputsWritten> outputs;
    std::vector<ssim::core::AlarmSet> alarms;
    std::vector<ProcessState> states;

private:
    ssim::core::EventBus& bus_;
    std::vector<ssim::core::EventBus::SubscriptionId> ids_;
    std::mutex mutex_;
    std::condition_variable cv_;
};

class MachineRuntimeTest : public ::testing::Test {
protected:
    void SetUp() override {
        static int counter = 0;
        root_ =
            std::filesystem::temp_directory_path() /
            ("ssim_runtime_test_" + std::to_string(::getpid()) + "_" + std::to_string(counter++));
        std::filesystem::remove_all(root_);
        config_.scan.realtime_factor = 0.0;
    }
    void TearDown() override { std::filesystem::remove_all(root_); }

    std::unique_ptr<MachineRuntime> make() {
        auto runtime = MachineRuntime::create(config_, root_);
        EXPECT_TRUE(runtime);
        return std::move(runtime).value();
    }

    ssim::core::Config config_;
    std::filesystem::path root_;
};

// SM1 through the whole runtime: hidden truth in, recovered value out.
TEST_F(MachineRuntimeTest, StartProducesResultWithinTwoPercentOfTruth) {
    auto runtime = make();
    Recorder rec(runtime->bus());

    ASSERT_TRUE(runtime->api().start(runtime->next_wafer_id(), 1, CommandSource::kUi));
    ASSERT_TRUE(rec.wait_until([&] { return !rec.results.empty() && !rec.maps.empty(); }));

    const auto& result = rec.results.front();
    EXPECT_EQ(result.wafer_id, "W001");
    EXPECT_NEAR(result.stress_mpa, config_.wafer.truth.stress_mpa,
                std::abs(config_.wafer.truth.stress_mpa) * 0.02);
    EXPECT_GT(result.stress_unc_mpa, 0.0);
    ASSERT_NE(rec.maps.front().map, nullptr);
    EXPECT_GT(rec.maps.front().map->width, 0);

    ASSERT_TRUE(rec.wait_until([&] { return !rec.outputs.empty(); }));
    EXPECT_TRUE(rec.outputs.front().ok);
    EXPECT_TRUE(std::filesystem::exists(rec.outputs.front().dir / "summary.json"));
}

TEST_F(MachineRuntimeTest, SecondWaferRunsAfterTheFirstWithItsOwnDirectory) {
    auto runtime = make();
    Recorder rec(runtime->bus());

    ASSERT_TRUE(runtime->api().start(runtime->next_wafer_id(), 1, CommandSource::kUi));
    ASSERT_TRUE(rec.wait_for_state_count(ProcessState::kIdle, 1));
    ASSERT_TRUE(runtime->api().start(runtime->next_wafer_id(), 1, CommandSource::kUi));
    ASSERT_TRUE(rec.wait_until([&] { return rec.outputs.size() == 2; }));

    EXPECT_EQ(rec.results[0].wafer_id, "W001");
    EXPECT_EQ(rec.results[1].wafer_id, "W002");
    ASSERT_TRUE(rec.wait_for_state_count(ProcessState::kIdle, 2));
    EXPECT_TRUE(std::filesystem::exists(runtime->run_dir() / "W001"));
    EXPECT_TRUE(std::filesystem::exists(runtime->run_dir() / "W002"));
}

// IT-CTRL-2: Start during an alarm is refused; ClearAlarm allows the next one.
TEST_F(MachineRuntimeTest, SensorFaultRaisesAlarmBlocksStartUntilCleared) {
    ssim::core::FaultConfig fault;
    fault.wafer = "W002";
    fault.type = "spike";
    fault.rate = 0.05;
    fault.amplitude_um = 40.0;
    config_.faults.push_back(fault);

    auto runtime = make();
    Recorder rec(runtime->bus());

    ASSERT_TRUE(runtime->api().start(runtime->next_wafer_id(), 1, CommandSource::kUi));
    ASSERT_TRUE(rec.wait_for_state_count(ProcessState::kIdle, 1));

    ASSERT_TRUE(runtime->api().start(runtime->next_wafer_id(), 1, CommandSource::kUi));
    ASSERT_TRUE(rec.wait_until([&] { return !rec.alarms.empty(); }));
    ASSERT_TRUE(rec.wait_for_state_count(ProcessState::kAlarm, 1));
    EXPECT_EQ(rec.alarms.front().alid, 1001);
    EXPECT_EQ(rec.results.size(), 1u);  // no number for the faulty wafer

    auto blocked = runtime->api().start(runtime->next_wafer_id(), 1, CommandSource::kUi);
    ASSERT_FALSE(blocked);
    EXPECT_EQ(blocked.error().code, ssim::core::kReasonIllegalTransition);

    ASSERT_TRUE(runtime->api().clear_alarm(CommandSource::kUi));
    ASSERT_TRUE(runtime->api().start(runtime->next_wafer_id(), 1, CommandSource::kUi));
    ASSERT_TRUE(rec.wait_until([&] { return rec.results.size() == 2; }));
    EXPECT_EQ(rec.results[1].wafer_id, "W003");
}

TEST_F(MachineRuntimeTest, AbortDuringScanReturnsToIdleAndAllowsAnotherRun) {
    config_.scan.realtime_factor = 1.0;
    auto runtime = make();
    Recorder rec(runtime->bus());

    ASSERT_TRUE(runtime->api().start(runtime->next_wafer_id(), 1, CommandSource::kUi));
    ASSERT_TRUE(runtime->api().abort(CommandSource::kUi));
    ASSERT_TRUE(rec.wait_for_state_count(ProcessState::kIdle, 1));
    EXPECT_TRUE(rec.results.empty());
    EXPECT_EQ(runtime->api().snapshot().process_state, ProcessState::kIdle);

    // Stale blocks from the aborted run must not leak into the next one.
    ASSERT_TRUE(
        runtime->api().set_control_mode(ssim::core::ControlMode::kOnlineLocal, CommandSource::kUi));
    ASSERT_TRUE(runtime->api().start(runtime->next_wafer_id(), 1, CommandSource::kUi));
    ASSERT_TRUE(runtime->api().abort(CommandSource::kUi));
    ASSERT_TRUE(rec.wait_for_state_count(ProcessState::kIdle, 2));
}

// FT-SHUTDOWN-1: destroying the runtime mid-scan joins every thread in time.
TEST_F(MachineRuntimeTest, DestroyingWhileScanningFinishesWithinTwoSeconds) {
    config_.scan.realtime_factor = 1.0;
    auto runtime = make();
    ASSERT_TRUE(runtime->api().start(runtime->next_wafer_id(), 1, CommandSource::kUi));

    const auto begin = std::chrono::steady_clock::now();
    runtime.reset();
    const auto elapsed = std::chrono::steady_clock::now() - begin;
    EXPECT_LT(elapsed, std::chrono::seconds(2));
}

// FR-MC-3 at run time: with comm.enabled=false the machine has no listener and
// still scans, whatever the build contains.
TEST_F(MachineRuntimeTest, WithCommDisabledThereIsNoListenerAndTheMachineStillWorks) {
    config_.comm.enabled = false;
    RuntimeOptions options;
    options.start_comm = true;
    auto created = MachineRuntime::create(config_, root_, options);
    ASSERT_TRUE(created);
    auto runtime = std::move(created).value();
    EXPECT_EQ(runtime->hsms_port(), 0);

    Recorder rec(runtime->bus());
    ASSERT_TRUE(runtime->api().start(runtime->next_wafer_id(), 1, CommandSource::kUi));
    ASSERT_TRUE(rec.wait_until([&] { return !rec.results.empty(); }));
}

#ifdef SSIM_HAS_SECSGEM
TEST_F(MachineRuntimeTest, WithCommEnabledTheOsChoosesThePortWhenAskedForZero) {
    config_.comm.enabled = true;
    config_.comm.port = 0;
    RuntimeOptions options;
    options.start_comm = true;
    auto created = MachineRuntime::create(config_, root_, options);
    ASSERT_TRUE(created) << created.error().message;
    EXPECT_NE(created.value()->hsms_port(), 0);
}

TEST_F(MachineRuntimeTest, ABadBindAddressIsAnErrorValueNotACrash) {
    config_.comm.enabled = true;
    config_.comm.bind = "not-an-address";
    RuntimeOptions options;
    options.start_comm = true;
    EXPECT_FALSE(MachineRuntime::create(config_, root_, options));
}
#endif

}  // namespace
}  // namespace ssim::machine
