#include "ssim/hw/scan_thread.hpp"

#include <chrono>
#include <cmath>
#include <optional>

#include "ssim/core/events.hpp"
#include "ssim/core/logger.hpp"

namespace ssim::hw {

namespace {
constexpr double kPi = 3.14159265358979323846;

std::optional<FaultType> parse_fault_type(const std::string& type) {
    if (type == "spike") return FaultType::kSpike;
    if (type == "burst") return FaultType::kBurst;
    if (type == "dropout") return FaultType::kDropout;
    if (type == "stuck") return FaultType::kStuck;
    if (type == "drift") return FaultType::kDrift;
    if (type == "saturation") return FaultType::kSaturation;
    if (type == "stage_stall") return FaultType::kStageStall;
    return std::nullopt;
}

FaultSpec to_fault_spec(const ssim::core::FaultConfig& fc, FaultType type) {
    FaultSpec spec;
    spec.type = type;
    spec.rate = fc.rate;
    spec.amplitude_um = fc.amplitude_um;
    spec.length_samples = fc.length_samples;
    spec.drift_um_per_s = fc.drift_um_per_s;
    spec.saturation_limit_um = fc.saturation_limit_um;
    spec.stall_duration_s = fc.stall_duration_s;
    return spec;
}

struct ActiveFault {
    FaultInjector injector;
    double stall_duration_s;
};
}  // namespace

ScanThread::ScanThread(IStage& stage, ILaserSensor& laser, const WaferModel& wafer_model,
                       ssim::core::ScanConfig scan_config,
                       std::vector<ssim::core::FaultConfig> fault_configs,
                       ssim::core::BoundedQueue<ssim::core::SampleBlock>& sample_queue,
                       ssim::core::EventBus& bus, ssim::core::AlarmManager& alarms,
                       std::function<void(std::string)> on_lines_complete,
                       std::function<void()> on_stopped)
    : stage_(stage),
      laser_(laser),
      wafer_model_(wafer_model),
      scan_config_(std::move(scan_config)),
      fault_configs_(std::move(fault_configs)),
      sample_queue_(sample_queue),
      bus_(bus),
      alarms_(alarms),
      on_lines_complete_(std::move(on_lines_complete)),
      on_stopped_(std::move(on_stopped)) {}

ScanThread::~ScanThread() {
    request_abort();
    join();
}

void ScanThread::start(std::string wafer_id) {
    abort_requested_.store(false);
    thread_ =
        std::thread([this, wafer_id = std::move(wafer_id)]() mutable { run(std::move(wafer_id)); });
}

void ScanThread::request_abort() { abort_requested_.store(true); }

void ScanThread::join() {
    if (thread_.joinable()) {
        thread_.join();
    }
}

void ScanThread::run(std::string wafer_id) {
    ssim::core::set_current_thread_name("scan");

    std::vector<ActiveFault> injectors;
    for (const auto& fc : fault_configs_) {
        if (fc.wafer != wafer_id) {
            continue;
        }
        auto type = parse_fault_type(fc.type);
        if (!type) {
            continue;
        }
        injectors.push_back(ActiveFault{
            FaultInjector(to_fault_spec(fc, *type), wafer_model_.seed()), fc.stall_duration_s});
    }

    const double radius_m = wafer_model_.truth().diameter_m / 2.0;
    const int n_points = static_cast<int>(radius_m * 2.0 * 1000.0 * scan_config_.points_per_mm) + 1;
    const int n_lines = scan_config_.lines;

    std::size_t sample_index = 0;
    const auto scan_start = std::chrono::steady_clock::now();
    bool aborted = false;

    for (int line = 0; line < n_lines && !aborted; ++line) {
        const double theta = kPi * static_cast<double>(line) / static_cast<double>(n_lines);
        stage_.set_line(theta);

        ssim::core::SampleBlock block;
        block.line_index = static_cast<std::size_t>(line);
        block.angle_rad = theta;
        block.positions_m.reserve(static_cast<std::size_t>(n_points));
        block.heights_m.reserve(static_cast<std::size_t>(n_points));
        block.timestamps.reserve(static_cast<std::size_t>(n_points));

        for (int p = 0; p < n_points; ++p) {
            if (abort_requested_.load()) {
                aborted = true;
                break;
            }
            const double s = -radius_m + 2.0 * radius_m * static_cast<double>(p) / (n_points - 1);
            stage_.move_to(s);
            const double raw = laser_.read_height_m();

            double height = raw;
            bool dropped = false;
            for (auto& active : injectors) {
                const auto elapsed_s =
                    std::chrono::duration<double>(std::chrono::steady_clock::now() - scan_start)
                        .count();
                auto sample = active.injector.apply(sample_index, elapsed_s, height);
                if (!sample.height_m.has_value()) {
                    dropped = true;
                    break;
                }
                height = *sample.height_m;
                if (sample.stage_stall) {
                    alarms_.set(ssim::core::AlarmId::kScanStall, "stage stall fault triggered");
                    if (scan_config_.realtime_factor > 0.0 && active.stall_duration_s > 0.0) {
                        std::this_thread::sleep_for(std::chrono::duration<double>(
                            active.stall_duration_s / scan_config_.realtime_factor));
                    }
                    alarms_.clear(ssim::core::AlarmId::kScanStall);
                }
            }
            ++sample_index;

            block.positions_m.push_back(s);
            block.heights_m.push_back(dropped ? std::nan("") : height);
            block.timestamps.push_back(std::chrono::steady_clock::now());
        }

        const double percent =
            100.0 * static_cast<double>(line + (aborted ? 0 : 1)) / static_cast<double>(n_lines);
        bus_.publish(ssim::core::ScanProgress{wafer_id, percent});

        if (!sample_queue_.push(std::move(block)) && !aborted) {
            alarms_.set(ssim::core::AlarmId::kQueueOverflow, "sample queue full, block dropped");
        }
    }

    if (aborted) {
        on_stopped_();
    } else {
        on_lines_complete_(wafer_id);
    }
}

}  // namespace ssim::hw
