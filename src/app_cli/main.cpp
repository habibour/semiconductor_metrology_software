// equipment_cli: runs the machine headlessly, without Qt, through the same
// MachineRuntime the Qt panel and the SECS/GEM link use (FR-CLI-1). The
// composition of ssim_core, ssim_hw and ssim_analysis lives in MachineRuntime;
// this file only parses arguments and reports.

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <future>
#include <iostream>
#include <optional>
#include <string>
#include <thread>

#include "ssim/core/command.hpp"
#include "ssim/core/config.hpp"
#include "ssim/core/events.hpp"
#include "ssim/machine/machine_events.hpp"
#include "ssim/machine/machine_runtime.hpp"

namespace {

// FR-CLI-2's exit codes: 0 success, 2 configuration error, 3 runtime fault,
// 4 interrupted (serve mode, on SIGINT or SIGTERM).
constexpr int kExitOk = 0;
constexpr int kExitConfigError = 2;
constexpr int kExitRuntimeFault = 3;
constexpr int kExitInterrupted = 4;

struct CliOptions {
    std::optional<std::string> config_path;
    std::optional<int> port;
    std::optional<std::uint64_t> seed;
    std::optional<double> rtf;
    std::optional<std::string> out_dir;
    std::optional<std::string> scenario;
    bool demo = false;
    bool serve = false;
    std::optional<std::string> control;  // serve mode: offline | local | remote
};

// FR-CLI-1's declared option surface: --config, --port, --seed, --rtf,
// --out, --scenario, plus the `demo` mode. Hand-parsed: no CLI library is
// on the allowed-dependency list (CLAUDE.md §6.10) and this flag set is
// small enough not to need one.
std::optional<CliOptions> parse_args(int argc, char** argv) {
    CliOptions opts;
    auto next_value = [&](int& i) -> std::optional<std::string> {
        if (i + 1 >= argc) {
            return std::nullopt;
        }
        return std::string(argv[++i]);
    };
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "demo") {
            opts.demo = true;
        } else if (arg == "serve") {
            opts.serve = true;
        } else if (arg == "--control") {
            auto v = next_value(i);
            if (!v) return std::nullopt;
            opts.control = *v;
        } else if (arg == "--config") {
            auto v = next_value(i);
            if (!v) return std::nullopt;
            opts.config_path = *v;
        } else if (arg == "--port") {
            auto v = next_value(i);
            if (!v) return std::nullopt;
            opts.port = std::stoi(*v);
        } else if (arg == "--seed") {
            auto v = next_value(i);
            if (!v) return std::nullopt;
            opts.seed = std::stoull(*v);
        } else if (arg == "--rtf") {
            auto v = next_value(i);
            if (!v) return std::nullopt;
            opts.rtf = std::stod(*v);
        } else if (arg == "--out") {
            auto v = next_value(i);
            if (!v) return std::nullopt;
            opts.out_dir = *v;
        } else if (arg == "--scenario") {
            auto v = next_value(i);
            if (!v) return std::nullopt;
            opts.scenario = *v;
        } else {
            std::cerr << "unrecognized argument: " << arg << "\n";
            return std::nullopt;
        }
    }
    return opts;
}

// Loads the configuration (file, then command-line overrides). Prints the
// problem and returns nullopt on a configuration error (exit code 2).
std::optional<ssim::core::Config> build_config(const CliOptions& opts) {
    ssim::core::Config config;
    if (opts.config_path) {
        auto loaded = ssim::core::load_config_file(*opts.config_path);
        if (!loaded) {
            std::cerr << "config error: " << loaded.error().message << "\n";
            return std::nullopt;
        }
        for (const auto& warning : loaded.value().warnings) {
            std::cerr << "config warning: " << warning << "\n";
        }
        config = loaded.value().config;
    }
    if (opts.port) config.comm.port = *opts.port;
    if (opts.seed) config.wafer.seed = *opts.seed;
    if (opts.rtf) config.scan.realtime_factor = *opts.rtf;
    if (opts.out_dir) config.output.dir = *opts.out_dir;
    return config;
}

std::atomic<bool> g_stop_requested{false};
extern "C" void on_signal(int) { g_stop_requested.store(true); }

// FR-CLI-1 "serve": runs the machine with the optional SECS/GEM link until
// interrupted. Prints the port actually listening (--port 0 lets the OS pick),
// so a script or test can connect to it.
int run_serve(const CliOptions& opts) {
    auto config = build_config(opts);
    if (!config) {
        return kExitConfigError;
    }
    ssim::core::ControlMode mode = ssim::core::ControlMode::kOnlineLocal;
    if (opts.control) {
        if (*opts.control == "offline") {
            mode = ssim::core::ControlMode::kOffline;
        } else if (*opts.control == "local") {
            mode = ssim::core::ControlMode::kOnlineLocal;
        } else if (*opts.control == "remote") {
            mode = ssim::core::ControlMode::kOnlineRemote;
        } else {
            std::cerr << "config error: --control must be offline, local or remote\n";
            return kExitConfigError;
        }
    }

    ssim::machine::RuntimeOptions runtime_options;
    runtime_options.start_comm = true;
    auto runtime =
        ssim::machine::MachineRuntime::create(*config, config->output.dir, runtime_options);
    if (!runtime) {
        std::cerr << "runtime fault: " << runtime.error().message << "\n";
        return kExitRuntimeFault;
    }
    auto& machine = *runtime.value();
    if (machine.hsms_port() == 0) {
        std::cout << "SECS/GEM link is disabled (comm.enabled=false); serving standalone\n";
    }
    auto set_mode = machine.api().set_control_mode(mode, ssim::core::CommandSource::kCli);
    if (!set_mode) {
        std::cerr << "runtime fault: " << set_mode.error().message << "\n";
        return kExitRuntimeFault;
    }
    if (machine.hsms_port() != 0) {
        std::cout << "listening on " << config->comm.bind << ":" << machine.hsms_port()
                  << std::endl;
    }
    std::cout << "output in " << machine.run_dir().string() << std::endl;

    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);
    while (!g_stop_requested.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    std::cout << "interrupted, shutting down\n";
    return kExitInterrupted;
}

// One standalone wafer through the same MachineRuntime that serve mode and the
// panel use, so all three run the same controller, alarm rules and writers
// (FR-CLI-1). Prints what the run reports; the exit code is 0 unless the machine
// or the output files failed (an alarm is a reported outcome, not a fault).
int run_once(const ssim::core::Config& config) {
    auto runtime = ssim::machine::MachineRuntime::create(config, config.output.dir);
    if (!runtime) {
        std::cerr << "runtime fault: " << runtime.error().message << "\n";
        return kExitRuntimeFault;
    }
    auto& machine = *runtime.value();

    // Handlers run on the machine's threads; everything they record is read only
    // after the outputs event, which is published after the result and any alarm
    // by the same processing thread (the alarm inside the controller call it makes
    // synchronously).
    std::optional<ssim::core::WaferResultReady> result;
    std::string alarm_reason;
    std::promise<ssim::machine::WaferOutputsWritten> written;
    auto& bus = machine.bus();
    const auto result_sub = bus.subscribe<ssim::core::WaferResultReady>(
        [&result](const ssim::core::WaferResultReady& e) { result = e; });
    const auto alarm_sub = bus.subscribe<ssim::core::AlarmSet>(
        [&alarm_reason](const ssim::core::AlarmSet& e) { alarm_reason = e.reason; });
    const auto written_sub = bus.subscribe<ssim::machine::WaferOutputsWritten>(
        [&written](const ssim::machine::WaferOutputsWritten& e) { written.set_value(e); });

    const std::string wafer_id = machine.next_wafer_id();
    auto started = machine.api().start(wafer_id, 1, ssim::core::CommandSource::kCli);
    if (!started) {
        std::cerr << "runtime fault: Start rejected: " << started.error().message << "\n";
        return kExitRuntimeFault;
    }

    const ssim::machine::WaferOutputsWritten outputs = written.get_future().get();
    bus.unsubscribe(result_sub);
    bus.unsubscribe(alarm_sub);
    bus.unsubscribe(written_sub);

    if (result) {
        std::cout << "wafer " << wafer_id << ": stress = " << result->stress_mpa << " MPa (+/- "
                  << result->stress_unc_mpa
                  << "), out_of_spec=" << (result->out_of_spec ? "true" : "false") << "\n";
    } else {
        std::cout << "wafer " << wafer_id << ": alarm raised, no stress number reported ("
                  << alarm_reason << ")\n";
    }
    std::cout << "results written to " << outputs.dir.string() << "\n";
    if (!outputs.ok) {
        std::cerr << "runtime fault: one or more output files failed to write\n";
        return kExitRuntimeFault;
    }
    return kExitOk;
}

}  // namespace

int main(int argc, char** argv) {
    auto maybe_opts = parse_args(argc, argv);
    if (!maybe_opts) {
        std::cerr << "usage: equipment_cli [demo|serve] [--config PATH] [--port N] [--seed N] "
                     "[--rtf X] [--out DIR] [--scenario NAME] [--control offline|local|remote]\n";
        return kExitConfigError;
    }
    CliOptions opts = *maybe_opts;

    if (opts.serve) {
        return run_serve(opts);
    }

    if (opts.demo) {
        // FR-CLI-3 describes demo mode as running "against a local host
        // simulator" — host_sim doesn't exist until Day 5, so today this
        // is the same standalone run as without the flag, made explicit
        // rather than silently claiming host interaction that isn't built.
        std::cout << "demo mode: standalone run (no host — SECS/GEM is Day 4/5 scope)\n";
    }
    if (opts.scenario) {
        // Day 2 scope: the *.scn scenario format is Day 4/5 work (PRD §6.7
        // scenarios/, driven by host_sim). Accepting and naming the flag
        // now keeps the option surface FR-CLI-1 lists stable; it is not
        // yet wired to anything.
        std::cerr << "note: --scenario is accepted but not yet implemented (Day 4/5 scope); "
                     "ignoring '"
                  << *opts.scenario << "'\n";
    }

    auto built_config = build_config(opts);
    if (!built_config) {
        return kExitConfigError;
    }
    ssim::core::Config config = *built_config;

    return run_once(config);
}
