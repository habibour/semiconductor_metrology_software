#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>

#include "ssim/analysis/writers/csv_writer.hpp"
#include "ssim/analysis/writers/json_writer.hpp"
#include "ssim/analysis/writers/png_writer.hpp"

namespace ssim::analysis {
namespace {

class Writers : public ::testing::Test {
protected:
    void SetUp() override {
        dir_ = std::filesystem::temp_directory_path() /
               ("ssim_writers_test_" +
                std::to_string(::testing::UnitTest::GetInstance()->random_seed()) + "_" +
                test_info_name());
        std::filesystem::create_directories(dir_);
    }
    void TearDown() override { std::filesystem::remove_all(dir_); }

    std::string test_info_name() {
        return ::testing::UnitTest::GetInstance()->current_test_info()->name();
    }

    PipelineResult make_result() {
        PipelineResult result;
        LineFitResult fit;
        fit.angle_rad = 0.0;
        fit.a = -0.005;
        fit.b = 0.001;
        fit.c = 1e-6;
        fit.curvature_per_m = -0.01;
        fit.se_curvature_per_m = 1e-5;
        fit.residual_rms_m = 0.4e-6;
        fit.n_used = 100;
        fit.valid = true;
        result.line_fits.push_back(fit);

        FilterResult filtered;
        filtered.angle_rad = 0.0;
        filtered.samples.push_back(FilteredSample{-0.1, 0.0, SampleFlag::kKept});
        filtered.samples.push_back(FilteredSample{-0.099, 0.0, SampleFlag::kEdgeExcluded});
        filtered.removed_count = 1;
        filtered.removed_fraction = 0.5;
        result.filtered_lines.push_back(filtered);

        result.combined.mean_curvature_per_m = -0.01;
        result.combined.anisotropy = 0.05;
        result.stress_pa = -180e6;
        result.stress_unc_pa = 0.6e6;
        result.max_fit_rms_m = 0.4e-6;
        result.removed_fraction_overall = 0.5;
        result.quality.issue = QualityIssue::kNone;
        result.quality.out_of_spec = false;

        result.map.width = 3;
        result.map.height = 3;
        result.map.grid_mm = 100.0;
        result.map.diameter_m = 0.3;
        result.map.heights_m = {std::nan(""), 1e-6,         std::nan(""), 2e-6,        3e-6,
                                4e-6,         std::nan(""), 5e-6,         std::nan("")};
        return result;
    }

    std::filesystem::path dir_;
};

TEST_F(Writers, JsonSummaryContainsExpectedFields) {
    WaferRunMeta meta{"20260101-000000", "W001", 1, "C001", 12345, -180.0, 100, 5};
    auto result = write_json_summary(dir_, make_result(), meta);

    ASSERT_TRUE(result);
    std::ifstream in(result.value());
    nlohmann::json j;
    in >> j;
    EXPECT_EQ(j["wafer_id"], "W001");
    EXPECT_NEAR(j["result"]["stress_mpa"].get<double>(), -180.0, 1e-9);
    EXPECT_EQ(j["simulation_truth"]["seed"], 12345);
}

TEST_F(Writers, JsonSummaryRefusesToOverwrite) {
    WaferRunMeta meta{"r", "W001", 1, "C001", 1, -180.0, 0, 0};
    ASSERT_TRUE(write_json_summary(dir_, make_result(), meta));
    auto second = write_json_summary(dir_, make_result(), meta);
    EXPECT_FALSE(second);
}

TEST_F(Writers, LineCsvHasHeaderAndOneRowPerLine) {
    auto result = write_line_csv(dir_, "W001", make_result());
    ASSERT_TRUE(result);

    std::ifstream in(result.value());
    std::string header;
    std::getline(in, header);
    EXPECT_NE(header.find("curvature_1_per_m"), std::string::npos);

    int rows = 0;
    std::string line;
    while (std::getline(in, line)) ++rows;
    EXPECT_EQ(rows, 1);
}

TEST_F(Writers, SampleCsvHasHeaderAndFlagColumn) {
    auto result = write_sample_csv(dir_, "W001", make_result(), /*decimation=*/1);
    ASSERT_TRUE(result);

    std::ifstream in(result.value());
    std::string header;
    std::getline(in, header);
    EXPECT_NE(header.find("flag"), std::string::npos);

    int rows = 0;
    std::string line;
    while (std::getline(in, line)) ++rows;
    EXPECT_EQ(rows, 2);  // one kept + one edge-excluded sample
}

TEST_F(Writers, PngWriterProducesAValidPngFile) {
    auto result = write_wafer_map_png(dir_, make_result().map, /*edge_exclusion_mm=*/3.0);
    ASSERT_TRUE(result);

    std::ifstream in(result.value(), std::ios::binary);
    unsigned char magic[8] = {};
    in.read(reinterpret_cast<char*>(magic), 8);
    const unsigned char png_magic[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    EXPECT_TRUE(std::equal(std::begin(magic), std::end(magic), std::begin(png_magic)));
}

}  // namespace
}  // namespace ssim::analysis
