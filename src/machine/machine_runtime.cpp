#include "ssim/machine/machine_runtime.hpp"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <optional>
#include <system_error>
#include <utility>
#include <vector>

#include "ssim/analysis/pipeline.hpp"
#include "ssim/analysis/writers/csv_writer.hpp"
#include "ssim/analysis/writers/json_writer.hpp"
#include "ssim/analysis/writers/png_writer.hpp"
#include "ssim/core/events.hpp"
#include "ssim/machine/machine_events.hpp"

namespace ssim::machine {

namespace {

// Duplicated from equipment_cli's main(); folding the CLI onto this library
// (and removing the copy) is the planned Day 6 refactor.
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

std::string format_wafer_id(int n) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "W%03d", n);
    return std::string(buf);
}

}  // namespace

ssim::core::Result<std::filesystem::path> create_run_dir(const std::filesystem::path& output_root) {
    std::error_code ec;
    std::filesystem::create_directories(output_root, ec);
    if (ec) {
        return ssim::core::Result<std::filesystem::path>::err(
            {ssim::analysis::kWriteErrorExitCode,
             "cannot create output directory: " + ec.message()});
    }
    const std::string base = make_run_id();
    for (int attempt = 1; attempt < 1000; ++attempt) {
        const std::string name = attempt == 1 ? base : base + "-" + std::to_string(attempt);
        const std::filesystem::path candidate = output_root / name;
        // create_directory reports false (no error) if it already existed,
        // which is exactly the never-overwrite check FR-OUT-5 asks for.
        if (std::filesystem::create_directory(candidate, ec) && !ec) {
            return ssim::core::Result<std::filesystem::path>::ok(candidate);
        }
        if (ec) {
            return ssim::core::Result<std::filesystem::path>::err(
                {ssim::analysis::kWriteErrorExitCode,
                 "cannot create run directory: " + ec.message()});
        }
    }
    return ssim::core::Result<std::filesystem::path>::err(
        {ssim::analysis::kWriteErrorExitCode, "no free run directory name"});
}

void MachineRuntime::RestartableScanDriver::start(std::string wafer_id) {
    // The previous run's thread has already reported completion (the state
    // machine only allows Start from Idle), so this join is short.
    inner_.join();
    inner_.start(std::move(wafer_id));
}

ssim::core::Result<std::unique_ptr<MachineRuntime>> MachineRuntime::create(
    ssim::core::Config config, const std::filesystem::path& output_root) {
    auto run_dir = create_run_dir(output_root);
    if (!run_dir) {
        return ssim::core::Result<std::unique_ptr<MachineRuntime>>::err(run_dir.error());
    }
    return ssim::core::Result<std::unique_ptr<MachineRuntime>>::ok(
        std::make_unique<MachineRuntime>(std::move(config), run_dir.value()));
}

MachineRuntime::MachineRuntime(ssim::core::Config config, std::filesystem::path run_dir)
    : config_(std::move(config)),
      run_dir_(std::move(run_dir)),
      run_id_(run_dir_.filename().string()),
      logger_({run_dir_ / "machine.log"}, clock_),
      wafer_model_(config_.wafer),
      hw_(ssim::hw::make_simulated_hardware(config_.scan, config_.noise, wafer_model_)),
      alarms_(bus_),
      sample_queue_(static_cast<std::size_t>(config_.scan.lines) + 1,
                    ssim::core::BackpressurePolicy::kBlock),
      jobs_(8, ssim::core::BackpressurePolicy::kBlock),
      pool_(static_cast<std::size_t>(config_.analysis.threads)),
      scan_thread_(
          *hw_.stage, *hw_.laser, wafer_model_, config_.scan, config_.faults, sample_queue_, bus_,
          alarms_,
          [this](std::string id) {
              controller_.notify_scan_complete(id);
              (void)jobs_.push(std::move(id));
          },
          [this] {
              // The scan thread is the only producer and has stopped, so
              // draining here cannot eat blocks of a later run (that run can
              // only start once notify_stage_stopped() has returned).
              drain_sample_queue();
              controller_.notify_stage_stopped();
          }),
      driver_(scan_thread_),
      controller_(bus_, driver_, alarms_) {
    logger_.start();

    bus_.subscribe<ssim::core::ScanStarted>([this](const ssim::core::ScanStarted& e) {
        wafers_started_.fetch_add(1);
        logger_.log(ssim::core::LogLevel::kInfo, "machine", "scan_started",
                    {{"wafer_id", e.wafer_id}});
    });
    bus_.subscribe<ssim::core::StateChanged>([this](const ssim::core::StateChanged& e) {
        logger_.log(ssim::core::LogLevel::kInfo, "machine", "state_change",
                    {{"from", ssim::core::to_string(e.from)}, {"to", ssim::core::to_string(e.to)}});
    });
    bus_.subscribe<ssim::core::ControlStateChanged>([this](
                                                        const ssim::core::ControlStateChanged& e) {
        logger_.log(ssim::core::LogLevel::kInfo, "machine", "control_state_change",
                    {{"from", ssim::core::to_string(e.from)}, {"to", ssim::core::to_string(e.to)}});
    });
    bus_.subscribe<ssim::core::AlarmSet>([this](const ssim::core::AlarmSet& e) {
        logger_.log(ssim::core::LogLevel::kWarn, "machine", "alarm_set",
                    {{"alid", e.alid}, {"name", e.name}, {"reason", e.reason}});
    });
    bus_.subscribe<ssim::core::AlarmCleared>([this](const ssim::core::AlarmCleared& e) {
        logger_.log(ssim::core::LogLevel::kInfo, "machine", "alarm_cleared",
                    {{"alid", e.alid}, {"name", e.name}});
    });

    controller_.start();
    api_ = std::make_unique<ssim::core::MachineApi>(controller_, bus_);
    worker_ = std::thread([this] { worker_loop(); });
}

MachineRuntime::~MachineRuntime() { shutdown(); }

// FR-MC-6 / CLAUDE.md §6.2: request stop, wake every waiter, join in reverse
// dependency order, flush the log. The worker is joined while the controller
// is still alive, because it posts results to the controller.
void MachineRuntime::shutdown() {
    if (shut_down_) {
        return;
    }
    shut_down_ = true;
    scan_thread_.request_abort();
    jobs_.close();
    sample_queue_.close();
    if (worker_.joinable()) {
        worker_.join();
    }
    controller_.stop();
    logger_.stop();
}

std::string MachineRuntime::next_wafer_id() const {
    return format_wafer_id(wafers_started_.load() + 1);
}

std::filesystem::path MachineRuntime::last_wafer_dir() const {
    std::lock_guard lock(dir_mutex_);
    return last_wafer_dir_;
}

void MachineRuntime::drain_sample_queue() {
    while (sample_queue_.size() > 0) {
        (void)sample_queue_.pop();
    }
}

void MachineRuntime::worker_loop() {
    ssim::core::set_current_thread_name("processing");
    while (auto wafer_id = jobs_.pop()) {
        process_wafer(*wafer_id);
    }
}

void MachineRuntime::process_wafer(const std::string& wafer_id) {
    using ssim::core::AlarmId;
    using ssim::core::ProcessState;

    std::vector<ssim::core::SampleBlock> lines;
    for (int i = 0; i < config_.scan.lines; ++i) {
        auto block = sample_queue_.pop();
        if (!block) {
            return;  // queue closed: shutting down
        }
        lines.push_back(std::move(*block));
    }

    // An Abort during Processing leaves the machine in Stopping with no
    // scan thread left to report "stage stopped", so it falls to us.
    if (controller_.state() == ProcessState::kStopping) {
        controller_.notify_stage_stopped();
        return;
    }

    const auto analysis_start = std::chrono::steady_clock::now();
    ssim::analysis::PipelineResult result = ssim::analysis::run_pipeline(lines, config_, pool_);
    const auto analysis_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now() - analysis_start)
                                 .count();

    // Quality failures first, then sensor-health alarms. All of them go
    // through the controller so the machine enters Alarm and blocks new
    // scans until cleared (FR-ALM-1, UC5).
    std::optional<AlarmId> raised;
    std::string reason;
    if (result.quality.issue == ssim::analysis::QualityIssue::kPoorFit) {
        raised = AlarmId::kFitQualityPoor;
        reason = "fit residual RMS exceeds fit_rms_limit_um";
    } else if (result.quality.issue == ssim::analysis::QualityIssue::kImplausible) {
        raised = AlarmId::kStressImplausible;
        reason = "stress non-finite or outside the plausibility window";
    } else if (result.outlier_removed_fraction > config_.analysis.outlier_fraction_alarm) {
        raised = AlarmId::kSensorSpikeRateHigh;
        reason = "removed fraction exceeds outlier_fraction_alarm";
    } else if (result.any_dropout) {
        raised = AlarmId::kSensorDropout;
        reason = "dropout gap present";
    }

    const double stress_mpa = result.stress_pa * 1e-6;
    controller_.notify_processing_result(result.quality.out_of_spec, stress_mpa, raised, reason,
                                         /*more_wafers_pending=*/false);
    if (controller_.state() == ProcessState::kStopping) {
        // Abort landed between the check above and the notify: the FSM
        // rejected the result, so discard it and finish the stop.
        controller_.notify_stage_stopped();
        return;
    }

    const std::filesystem::path wafer_dir = run_dir_ / wafer_id;
    std::error_code ec;
    std::filesystem::create_directories(wafer_dir, ec);
    {
        std::lock_guard lock(dir_mutex_);
        last_wafer_dir_ = wafer_dir;
    }

    if (!raised) {
        ssim::core::WaferResultReady ready;
        ready.wafer_id = wafer_id;
        ready.slot = 1;
        ready.stress_mpa = stress_mpa;
        ready.stress_unc_mpa = result.stress_unc_pa * 1e-6;
        ready.curvature_per_m = result.combined.mean_curvature_per_m;
        ready.fit_rms_um = result.max_fit_rms_m * 1e6;
        ready.out_of_spec = result.quality.out_of_spec;
        bus_.publish(ready);
        bus_.publish(WaferMapReady{wafer_id,
                                   std::make_shared<const ssim::analysis::WaferMap>(result.map),
                                   config_.scan.edge_exclusion_mm});
    }

    if (ec) {
        logger_.log(ssim::core::LogLevel::kError, "machine", "output_failed",
                    {{"wafer_id", wafer_id}, {"reason", ec.message()}});
        bus_.publish(WaferOutputsWritten{wafer_id, wafer_dir, false});
        return;
    }

    ssim::analysis::WaferRunMeta meta;
    meta.run_id = run_id_;
    meta.wafer_id = wafer_id;
    meta.slot = 1;
    meta.cassette_id = config_.cassette.id;
    meta.seed = wafer_model_.seed();
    meta.truth_stress_mpa = config_.wafer.truth.stress_mpa;
    meta.scan_ms = 0;  // not separately timed yet (same as equipment_cli)
    meta.analysis_ms = analysis_ms;

    bool ok = static_cast<bool>(ssim::analysis::write_json_summary(wafer_dir, result, meta));
    if (result.quality.issue == ssim::analysis::QualityIssue::kNone) {
        ok = static_cast<bool>(ssim::analysis::write_line_csv(wafer_dir, wafer_id, result)) && ok;
        ok = static_cast<bool>(ssim::analysis::write_sample_csv(
                 wafer_dir, wafer_id, result, config_.output.sample_decimation)) &&
             ok;
        ok = static_cast<bool>(ssim::analysis::write_wafer_map_png(
                 wafer_dir, result.map, config_.scan.edge_exclusion_mm)) &&
             ok;
    }
    if (!ok) {
        logger_.log(ssim::core::LogLevel::kError, "machine", "output_failed",
                    {{"wafer_id", wafer_id}, {"reason", "one or more files failed to write"}});
    }
    bus_.publish(WaferOutputsWritten{wafer_id, wafer_dir, ok});
}

}  // namespace ssim::machine
