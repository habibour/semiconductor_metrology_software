// Scenario tests ST-normal_run ... ST-t3_timeout (PRD 10.2). Each one starts a
// real machine with the SECS/GEM link on an OS-chosen port and runs the actual
// scenarios/*.scn script against it, over a real loopback socket, with the same
// runner the host_sim executable uses. On failure the whole message trace is
// printed.
//
// ST-cassette_run is not here: the cassette loop (FR-MC-5) is not built yet.

#include <gtest/gtest.h>
#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>

#include "ssim/host_sim/runner.hpp"
#include "ssim/host_sim/script.hpp"
#include "ssim/machine/machine_runtime.hpp"

#ifndef SSIM_SCENARIO_DIR
#error "SSIM_SCENARIO_DIR must be defined by CMake"
#endif

namespace ssim::scenario {
namespace {

using ssim::core::CommandSource;
using ssim::core::ControlMode;

class ScenarioTest : public ::testing::Test {
protected:
    void SetUp() override {
        static int counter = 0;
        root_ = std::filesystem::temp_directory_path() /
                ("ssim_scenario_" + std::to_string(::getpid()) + "_" + std::to_string(counter++));
        std::filesystem::remove_all(root_);
    }
    void TearDown() override { std::filesystem::remove_all(root_); }

    // Runs scenarios/<name>.scn against a machine set up as requested.
    void run(const std::string& name, ControlMode mode,
             const std::function<void(ssim::core::Config&)>& tweak = {}) {
        ssim::core::Config config;
        config.scan.realtime_factor = 0.0;  // scans as fast as possible
        config.comm.enabled = true;
        config.comm.bind = "127.0.0.1";
        config.comm.port = 0;
        config.output.dir = root_.string();
        if (tweak) tweak(config);

        ssim::machine::RuntimeOptions options;
        options.start_comm = true;
        auto created = ssim::machine::MachineRuntime::create(config, root_, options);
        ASSERT_TRUE(created) << created.error().message;
        auto machine = std::move(created).value();
        ASSERT_NE(machine->hsms_port(), 0);
        ASSERT_TRUE(machine->api().set_control_mode(mode, CommandSource::kUi));

        const std::string path = std::string(SSIM_SCENARIO_DIR) + "/" + name + ".scn";
        std::ifstream file(path);
        ASSERT_TRUE(file) << "cannot read " << path;
        std::stringstream text;
        text << file.rdbuf();

        auto commands = ssim::host_sim::parse_script(
            text.str(), {{"PORT", std::to_string(machine->hsms_port())}});
        ASSERT_TRUE(commands) << path << ": " << commands.error().message;

        std::ostringstream trace;
        ssim::host_sim::RunOptions run_options;
        run_options.device_id = static_cast<std::uint16_t>(config.comm.device_id);
        run_options.log = [&trace](const std::string& line) { trace << line << "\n"; };
        const auto result = ssim::host_sim::run_script(commands.value(), run_options);
        EXPECT_TRUE(result.ok) << name << ".scn line " << result.failed_line << ": "
                               << result.message << "\n--- message trace ---\n"
                               << trace.str();
    }

    std::filesystem::path root_;
};

TEST_F(ScenarioTest, NormalRun) { run("normal_run", ControlMode::kOnlineRemote); }

TEST_F(ScenarioTest, AlarmRecovery) {
    run("alarm_recovery", ControlMode::kOnlineRemote, [](ssim::core::Config& c) {
        ssim::core::FaultConfig fault;
        fault.wafer = "W002";
        fault.type = "spike";
        fault.rate = 0.05;
        fault.amplitude_um = 40.0;
        c.faults.push_back(fault);
    });
}

TEST_F(ScenarioTest, BadCommands) { run("bad_commands", ControlMode::kOnlineRemote); }

TEST_F(ScenarioTest, WrongControlState) { run("wrong_control_state", ControlMode::kOnlineLocal); }

TEST_F(ScenarioTest, MalformedFrames) { run("malformed_frames", ControlMode::kOnlineRemote); }

TEST_F(ScenarioTest, LinkLoss) { run("link_loss", ControlMode::kOnlineRemote); }

TEST_F(ScenarioTest, T3Timeout) {
    run("t3_timeout", ControlMode::kOnlineRemote, [](ssim::core::Config& c) { c.comm.t3_s = 1.0; });
}

}  // namespace
}  // namespace ssim::scenario
