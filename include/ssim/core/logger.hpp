#pragma once

// Thread-safety: Thread-safe. log() may be called from any thread; start()
// and stop() are intended to be called once each, from the thread that owns
// the Logger (typically main), and are safe to call from a different thread
// than log() as long as they are not called concurrently with each other.
//
// Owns exactly one thread ("logger"), reading a bounded queue (FR-LOG-1).
// log() never blocks the caller: records are pushed with the kDropNewest
// policy, so a slow or stalled logger thread cannot stall application
// threads. Dropped records are counted, never silently lost from the
// caller's point of view (dropped_count()).
//
// start()/stop() are separate from construction/destruction on purpose:
// it lets a caller queue records before the file is opened for writing
// (useful for startup messages), and it lets tests fill the queue to a known
// state before any consumer thread exists, so overflow behaviour is
// deterministic instead of racing a background thread.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>
#include <unordered_map>

#include "ssim/core/clock.hpp"
#include "ssim/core/queue.hpp"

namespace ssim::core {

enum class LogLevel { kDebug, kInfo, kWarn, kError };

const char* to_string(LogLevel level);

// Every thread that logs should call this once, near the top of its run
// loop, so log lines are attributable without needing OS-specific
// thread-naming APIs (CLAUDE.md §6.9: platform code lives only in
// src/platform/, which does not exist yet). Threads that never call this
// are recorded as "unnamed".
void set_current_thread_name(std::string name);
const std::string& current_thread_name();

struct LogRecord {
    std::chrono::steady_clock::time_point t_mono;
    std::chrono::system_clock::time_point t_wall;
    LogLevel level;
    std::string thread_name;
    std::string component;
    std::string event;
    nlohmann::json fields;  // object; empty object if no extra fields
};

struct LoggerConfig {
    std::filesystem::path path;
    std::size_t queue_capacity = 1024;
    std::size_t rotate_bytes = 10 * 1024 * 1024;  // FR-LOG-4
};

class Logger {
public:
    Logger(LoggerConfig config, const IClock& clock);
    ~Logger();

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    // Opens the first log file and starts the logger thread draining the
    // queue. Safe to call log() before start(): those records are queued
    // and written once the thread starts.
    void start();

    // Closes the queue, waits for the logger thread to drain it and flush
    // the file, then joins. Safe to call more than once; safe to call even
    // if start() was never called.
    void stop();

    void log(LogLevel level, std::string component, std::string event,
             nlohmann::json fields = nlohmann::json::object()) {
        LogRecord record{clock_.monotonic_now(), clock_.wall_now(),    level,
                         current_thread_name(),  std::move(component), std::move(event),
                         std::move(fields)};
        queue_.push(std::move(record));
    }

    std::size_t dropped_count() const { return queue_.dropped_count(); }

private:
    void run();
    void write_line(const LogRecord& record);
    void rotate_if_needed();

    LoggerConfig config_;
    const IClock& clock_;
    BoundedQueue<LogRecord> queue_;
    std::thread thread_;
    std::atomic<bool> started_{false};
    std::atomic<bool> stopped_{false};

    std::ofstream file_;
    std::size_t bytes_written_in_current_file_ = 0;
    int rotation_index_ = 0;
};

}  // namespace ssim::core
