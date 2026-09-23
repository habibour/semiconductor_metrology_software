// FR-CLI-1/3, FR-OUT-5: invokes the actual equipment_cli binary (not the
// libraries directly) and asserts on the files it produces, matching PRD
// §8.5's results/<run_id>/<wafer_id>/ layout end to end.

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include <nlohmann/json.hpp>

#ifndef EQUIPMENT_CLI_PATH
#error "EQUIPMENT_CLI_PATH must be defined by CMake"
#endif

namespace {

class CliDemo : public ::testing::Test {
protected:
    void SetUp() override {
        out_dir_ = std::filesystem::temp_directory_path() /
                  ("ssim_cli_demo_test_" +
                   std::to_string(::testing::UnitTest::GetInstance()->random_seed()) + "_" +
                   ::testing::UnitTest::GetInstance()->current_test_info()->name());
        std::filesystem::remove_all(out_dir_);
    }
    void TearDown() override { std::filesystem::remove_all(out_dir_); }

    // Finds the single run_id/wafer_id directory equipment_cli created.
    std::filesystem::path find_wafer_dir() const {
        for (const auto& run_entry : std::filesystem::directory_iterator(out_dir_)) {
            for (const auto& wafer_entry : std::filesystem::directory_iterator(run_entry.path())) {
                return wafer_entry.path();
            }
        }
        return {};
    }

    std::filesystem::path out_dir_;
};

TEST_F(CliDemo, ProducesJsonCsvAndPngUnderResultsRunIdWaferId) {
    const std::string command = std::string("\"") + EQUIPMENT_CLI_PATH +
                                "\" demo --rtf 0 --out \"" + out_dir_.string() + "\"";
    const int exit_code = std::system(command.c_str());

    ASSERT_EQ(exit_code, 0);
    const auto wafer_dir = find_wafer_dir();
    ASSERT_FALSE(wafer_dir.empty()) << "no results/<run_id>/<wafer_id>/ directory was created";

    EXPECT_TRUE(std::filesystem::exists(wafer_dir / "summary.json"));
    EXPECT_TRUE(std::filesystem::exists(wafer_dir / "lines.csv"));
    EXPECT_TRUE(std::filesystem::exists(wafer_dir / "samples.csv"));
    EXPECT_TRUE(std::filesystem::exists(wafer_dir / "wafer_map.png"));

    std::ifstream in(wafer_dir / "summary.json");
    nlohmann::json j;
    in >> j;
    EXPECT_EQ(j["wafer_id"], "W001");
    ASSERT_TRUE(j["result"]["stress_mpa"].is_number());
    // SM1: within 2% of the nominal -180 MPa truth.
    EXPECT_NEAR(j["result"]["stress_mpa"].get<double>(), -180.0, 180.0 * 0.02);
}

TEST_F(CliDemo, RunningTwiceIntoTheSameOutputDirNeverOverwritesAFile) {
    const std::string command = std::string("\"") + EQUIPMENT_CLI_PATH +
                                "\" demo --rtf 0 --out \"" + out_dir_.string() + "\"";
    ASSERT_EQ(std::system(command.c_str()), 0);
    ASSERT_EQ(std::system(command.c_str()), 0);  // different run_id (timestamp), so this must succeed

    int run_count = 0;
    for (const auto& entry : std::filesystem::directory_iterator(out_dir_)) {
        (void)entry;
        ++run_count;
    }
    EXPECT_EQ(run_count, 2);
}

}  // namespace
