// equipment_cli: runs the machine headlessly, without Qt, through the same
// core the (future) Qt panel and SECS/GEM module use (FR-CLI-1). This file
// is the composition root: it is the one place allowed to wire ssim_core,
// ssim_hw and ssim_analysis together for a single standalone run, since
// none of those libraries may depend on each other in that direction
// (PRD §6.2).

#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <future>
#include <iostream>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

#include "ssim/analysis/fit_pool.hpp"
#include "ssim/analysis/pipeline.hpp"
#include "ssim/analysis/writers/csv_writer.hpp"
#include "ssim/analysis/writers/json_writer.hpp"
#include "ssim/analysis/writers/png_writer.hpp"
#include "ssim/core/alarms.hpp"
#include "ssim/core/clock.hpp"
#include "ssim/core/command.hpp"
#include "ssim/core/config.hpp"
#include "ssim/core/controller.hpp"
#include "ssim/core/events.hpp"
#include "ssim/core/logger.hpp"
#include "ssim/core/queue.hpp"
#include "ssim/hw/hardware_factory.hpp"
#include "ssim/hw/scan_thread.hpp"
#include "ssim/hw/wafer_model.hpp"

namespace {

// FR-CLI-2's fourth code, 4 (interrupted), has no user yet: Day 2 installs
// no signal handler, so there is nothing that would produce it today.
constexpr int kExitOk = 0;
constexpr int kExitConfigError = 2;
constexpr int kExitRuntimeFault = 3;

struct CliOptions {
    std::optional<std::string> config_path;
    std::optional<int> port;
    std::optional<std::uint64_t> seed;
    std::optional<double> rtf;
    std::optional<std::string> out_dir;
    std::optional<std::string> scenario;
    bool demo = false;
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

std::string make_run_id() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t now_c = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf{};
#if defined(_WIN32)
    gmtime_s(&tm_buf, &now_c);
#else
    gmtime_r(&now_c, &tm_buf);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y%m%d-%H%M%S", &tm_buf);
    return std::string(buf);
}

}  // namespace

int main(int argc, char** argv) {
    auto maybe_opts = parse_args(argc, argv);
    if (!maybe_opts) {
        std::cerr << "usage: equipment_cli [demo] [--config PATH] [--port N] [--seed N] "
                    "[--rtf X] [--out DIR] [--scenario NAME]\n";
        return kExitConfigError;
    }
    CliOptions opts = *maybe_opts;

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

    ssim::core::Config config;
    if (opts.config_path) {
        auto loaded = ssim::core::load_config_file(*opts.config_path);
        if (!loaded) {
            std::cerr << "config error: " << loaded.error().message << "\n";
            return kExitConfigError;
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

    const std::string run_id = make_run_id();
    const std::string wafer_id = "W001";  // FR-CLI-1 declares no --wafer-id flag; Day 2 runs one wafer.
    const std::filesystem::path run_dir = std::filesystem::path(config.output.dir) / run_id;
    const std::filesystem::path wafer_dir = run_dir / wafer_id;

    if (std::filesystem::exists(wafer_dir)) {
        // FR-OUT-5: never overwrite an existing run. run_id has one-second
        // resolution, so this is a defensive check, not the expected path.
        std::cerr << "runtime fault: output directory already exists: " << wafer_dir.string()
                 << "\n";
        return kExitRuntimeFault;
    }
    std::error_code ec;
    std::filesystem::create_directories(wafer_dir, ec);
    if (ec) {
        std::cerr << "runtime fault: cannot create output directory: " << ec.message() << "\n";
        return kExitRuntimeFault;
    }

    ssim::core::SystemClock clock;
    ssim::core::Logger logger({run_dir / "machine.log"}, clock);
    logger.start();

    ssim::hw::WaferModel wafer_model(config.wafer);
    ssim::hw::SimulatedHardware hw =
        ssim::hw::make_simulated_hardware(config.scan, config.noise, wafer_model);

    ssim::core::EventBus bus;
    ssim::core::AlarmManager alarms(bus);
    ssim::core::BoundedQueue<ssim::core::SampleBlock> sample_queue(
        static_cast<std::size_t>(config.scan.lines) + 1, ssim::core::BackpressurePolicy::kBlock);

    bus.subscribe<ssim::core::StateChanged>([&logger](const ssim::core::StateChanged& e) {
        logger.log(ssim::core::LogLevel::kInfo, "machine", "state_change",
                  {{"from", ssim::core::to_string(e.from)}, {"to", ssim::core::to_string(e.to)}});
    });
    bus.subscribe<ssim::core::AlarmSet>([&logger](const ssim::core::AlarmSet& e) {
        logger.log(ssim::core::LogLevel::kWarn, "machine", "alarm_set",
                  {{"alid", e.alid}, {"name", e.name}, {"reason", e.reason}});
    });
    bus.subscribe<ssim::core::AlarmCleared>([&logger](const ssim::core::AlarmCleared& e) {
        logger.log(ssim::core::LogLevel::kInfo, "machine", "alarm_cleared",
                  {{"alid", e.alid}, {"name", e.name}});
    });

    std::promise<void> scan_done_promise;
    auto scan_done_future = scan_done_promise.get_future();
    // Set just below, once Controller exists; the callbacks only read it
    // when the scan thread actually finishes, which is well after that —
    // start()/submit_command() haven't even run yet at this point.
    ssim::core::Controller* controller_ptr = nullptr;

    ssim::hw::ScanThread scan_thread(
        *hw.stage, *hw.laser, wafer_model, config.scan, config.faults, sample_queue, bus, alarms,
        [&](std::string id) {
            controller_ptr->notify_scan_complete(std::move(id));
            scan_done_promise.set_value();
        },
        [&] {
            controller_ptr->notify_stage_stopped();
            scan_done_promise.set_value();
        });

    ssim::core::Controller controller(bus, scan_thread, alarms);
    controller_ptr = &controller;
    controller.start();

    auto start_result = controller.submit_command(
        ssim::core::Command{ssim::core::StartCommand{wafer_id, 1}, ssim::core::CommandSource::kCli, 1});
    if (!start_result) {
        std::cerr << "runtime fault: Start rejected: " << start_result.error().message << "\n";
        controller.stop();
        logger.stop();
        return kExitRuntimeFault;
    }

    scan_done_future.wait();

    if (controller.state() != ssim::core::ProcessState::kProcessing) {
        std::cerr << "runtime fault: scan did not complete (aborted or stalled)\n";
        controller.stop();
        logger.stop();
        return kExitRuntimeFault;
    }

    std::vector<ssim::core::SampleBlock> lines;
    for (int i = 0; i < config.scan.lines; ++i) {
        auto block = sample_queue.pop();
        if (!block) {
            break;
        }
        lines.push_back(std::move(*block));
    }

    ssim::analysis::FitThreadPool pool(static_cast<std::size_t>(config.analysis.threads));
    const auto analysis_start = std::chrono::steady_clock::now();
    ssim::analysis::PipelineResult result = ssim::analysis::run_pipeline(lines, config, pool);
    const auto analysis_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - analysis_start)
                                .count();

    if (result.outlier_removed_fraction > config.analysis.outlier_fraction_alarm) {
        alarms.set(ssim::core::AlarmId::kSensorSpikeRateHigh,
                  "removed fraction exceeds outlier_fraction_alarm");
    } else {
        alarms.clear(ssim::core::AlarmId::kSensorSpikeRateHigh);
    }
    if (result.any_dropout) {
        alarms.set(ssim::core::AlarmId::kSensorDropout, "dropout gap present");
    } else {
        alarms.clear(ssim::core::AlarmId::kSensorDropout);
    }

    std::optional<ssim::core::AlarmId> raised_alarm_id;
    std::string alarm_reason;
    if (result.quality.issue == ssim::analysis::QualityIssue::kPoorFit) {
        raised_alarm_id = ssim::core::AlarmId::kFitQualityPoor;
        alarm_reason = "fit residual RMS exceeds fit_rms_limit_um";
    } else if (result.quality.issue == ssim::analysis::QualityIssue::kImplausible) {
        raised_alarm_id = ssim::core::AlarmId::kStressImplausible;
        alarm_reason = "stress non-finite or outside the plausibility window";
    }
    controller.notify_processing_result(result.quality.out_of_spec, result.stress_pa * 1e-6,
                                        raised_alarm_id, alarm_reason,
                                        /*more_wafers_pending=*/false);

    ssim::analysis::WaferRunMeta meta;
    meta.run_id = run_id;
    meta.wafer_id = wafer_id;
    meta.slot = 1;
    meta.cassette_id = config.cassette.id;
    meta.seed = wafer_model.seed();
    meta.truth_stress_mpa = config.wafer.truth.stress_mpa;
    meta.scan_ms = 0;  // Day 2: not separately timed from analysis yet.
    meta.analysis_ms = analysis_ms;

    auto json_result = ssim::analysis::write_json_summary(wafer_dir, result, meta);
    if (!json_result) {
        std::cerr << "runtime fault: " << json_result.error().message << "\n";
    }

    int exit_code = kExitOk;
    if (result.quality.issue == ssim::analysis::QualityIssue::kNone) {
        auto line_csv = ssim::analysis::write_line_csv(wafer_dir, wafer_id, result);
        auto sample_csv = ssim::analysis::write_sample_csv(wafer_dir, wafer_id, result,
                                                            config.output.sample_decimation);
        auto png = ssim::analysis::write_wafer_map_png(wafer_dir, result.map,
                                                        config.scan.edge_exclusion_mm);
        if (!line_csv || !sample_csv || !png) {
            std::cerr << "runtime fault: one or more output files failed to write\n";
            exit_code = kExitRuntimeFault;
        } else {
            std::cout << "wafer " << wafer_id << ": stress = " << (result.stress_pa * 1e-6)
                     << " MPa (+/- " << (result.stress_unc_pa * 1e-6) << "), out_of_spec="
                     << (result.quality.out_of_spec ? "true" : "false") << "\n";
        }
    } else {
        std::cout << "wafer " << wafer_id << ": alarm raised, no stress number reported ("
                 << alarm_reason << ")\n";
    }

    std::cout << "results written to " << wafer_dir.string() << "\n";

    controller.stop();
    logger.stop();
    return exit_code;
}
