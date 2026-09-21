#include "ssim/core/logger.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "ssim/core/clock.hpp"

namespace ssim::core {
namespace {

std::filesystem::path make_temp_log_path(const std::string& name) {
    auto path = std::filesystem::temp_directory_path() / ("ssim_logger_test_" + name + ".jsonl");
    std::filesystem::remove(path);
    return path;
}

std::vector<std::string> read_lines(const std::filesystem::path& path) {
    std::ifstream in(path);
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty()) {
            lines.push_back(line);
        }
    }
    return lines;
}

TEST(Logger, WritesJsonLinesWithExpectedShape) {
    auto path = make_temp_log_path("shape");
    FakeClock clock;
    Logger logger(LoggerConfig{path, 16, 10 * 1024 * 1024}, clock);

    logger.start();
    logger.log(LogLevel::kInfo, "test_component", "test_event", {{"key", "value"}});
    logger.stop();

    auto lines = read_lines(path);
    ASSERT_EQ(lines.size(), 1u);
    auto parsed = nlohmann::json::parse(lines[0]);
    EXPECT_TRUE(parsed.contains("t_mono_ns"));
    EXPECT_TRUE(parsed.contains("t_wall"));
    EXPECT_TRUE(parsed.contains("thread"));
    EXPECT_EQ(parsed.at("level"), "info");
    EXPECT_EQ(parsed.at("component"), "test_component");
    EXPECT_EQ(parsed.at("event"), "test_event");
    EXPECT_EQ(parsed.at("fields").at("key"), "value");

    std::filesystem::remove(path);
}

// No consumer thread exists until start() is called, so these two log()
// calls are fully synchronous with the queue: overflow is deterministic,
// not a race with a background thread.
TEST(Logger, QueueOverflowDropsAndCountsBeforeStart) {
    auto path = make_temp_log_path("overflow");
    SystemClock clock;
    Logger logger(LoggerConfig{path, /*queue_capacity=*/1, 10 * 1024 * 1024}, clock);

    logger.log(LogLevel::kInfo, "c", "first");
    logger.log(LogLevel::kInfo, "c", "second");  // queue capacity 1 -> dropped

    EXPECT_EQ(logger.dropped_count(), 1u);

    logger.start();
    logger.stop();

    EXPECT_EQ(read_lines(path).size(), 1u);  // only the record that wasn't dropped

    std::filesystem::remove(path);
}

TEST(Logger, StopJoinsCleanlyAndIsIdempotent) {
    auto path = make_temp_log_path("shutdown");
    SystemClock clock;
    Logger logger(LoggerConfig{path, 16, 10 * 1024 * 1024}, clock);

    logger.start();
    logger.log(LogLevel::kDebug, "c", "e");
    logger.stop();
    logger.stop();  // must not hang or crash

    std::filesystem::remove(path);
}

TEST(Logger, RotatesWhenSizeLimitExceeded) {
    auto path = make_temp_log_path("rotate");
    SystemClock clock;
    Logger logger(LoggerConfig{path, 16, /*rotate_bytes=*/50},
                  clock);  // tiny limit forces rotation

    logger.start();
    for (int i = 0; i < 5; ++i) {
        logger.log(LogLevel::kInfo, "c", "event_" + std::to_string(i));
    }
    logger.stop();

    EXPECT_TRUE(std::filesystem::exists(path));
    EXPECT_TRUE(std::filesystem::exists(path.string() + ".1"));

    std::filesystem::remove(path);
    for (int i = 1; i <= 5; ++i) {
        std::filesystem::remove(path.string() + "." + std::to_string(i));
    }
}

}  // namespace
}  // namespace ssim::core
