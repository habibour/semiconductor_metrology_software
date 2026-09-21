#include "ssim/core/logger.hpp"

#include <iomanip>
#include <sstream>

namespace ssim::core {

namespace {
thread_local std::string g_thread_name = "unnamed";
}  // namespace

void set_current_thread_name(std::string name) { g_thread_name = std::move(name); }
const std::string& current_thread_name() { return g_thread_name; }

const char* to_string(LogLevel level) {
    switch (level) {
        case LogLevel::kDebug:
            return "debug";
        case LogLevel::kInfo:
            return "info";
        case LogLevel::kWarn:
            return "warn";
        case LogLevel::kError:
            return "error";
    }
    return "unknown";
}

namespace {
// Wall-clock time as an ISO-8601 UTC timestamp with millisecond precision.
std::string format_wall_time(std::chrono::system_clock::time_point tp) {
    const auto ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch()) % 1000;
    const std::time_t seconds = std::chrono::system_clock::to_time_t(tp);
    std::tm utc{};
#if defined(_WIN32)
    gmtime_s(&utc, &seconds);
#else
    gmtime_r(&seconds, &utc);
#endif
    std::ostringstream out;
    out << std::put_time(&utc, "%Y-%m-%dT%H:%M:%S");
    out << '.' << std::setfill('0') << std::setw(3) << ms.count() << 'Z';
    return out.str();
}
}  // namespace

Logger::Logger(LoggerConfig config, const IClock& clock)
    : config_(std::move(config)),
      clock_(clock),
      queue_(config_.queue_capacity, BackpressurePolicy::kDropNewest) {}

Logger::~Logger() { stop(); }

void Logger::start() {
    if (started_.exchange(true)) {
        return;
    }
    file_.open(config_.path, std::ios::out | std::ios::app | std::ios::binary);
    thread_ = std::thread([this] {
        set_current_thread_name("logger");
        run();
    });
}

void Logger::stop() {
    if (stopped_.exchange(true)) {
        return;
    }
    queue_.close();
    if (thread_.joinable()) {
        thread_.join();
    }
    if (file_.is_open()) {
        file_.flush();
        file_.close();
    }
}

void Logger::run() {
    while (auto record = queue_.pop()) {
        write_line(*record);
    }
}

void Logger::write_line(const LogRecord& record) {
    if (!file_.is_open()) {
        return;
    }
    const auto mono_ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(record.t_mono.time_since_epoch())
            .count();

    nlohmann::json line{
        {"t_mono_ns", mono_ns},
        {"t_wall", format_wall_time(record.t_wall)},
        {"level", to_string(record.level)},
        {"thread", record.thread_name},
        {"component", record.component},
        {"event", record.event},
        {"fields", record.fields},
    };

    const std::string text = line.dump();
    file_ << text << '\n';
    bytes_written_in_current_file_ += text.size() + 1;
    rotate_if_needed();
}

void Logger::rotate_if_needed() {
    if (bytes_written_in_current_file_ < config_.rotate_bytes) {
        return;
    }
    file_.flush();
    file_.close();

    ++rotation_index_;
    auto rotated_path = config_.path;
    rotated_path += "." + std::to_string(rotation_index_);
    std::filesystem::rename(config_.path, rotated_path);

    file_.open(config_.path, std::ios::out | std::ios::app | std::ios::binary);
    bytes_written_in_current_file_ = 0;
}

}  // namespace ssim::core
