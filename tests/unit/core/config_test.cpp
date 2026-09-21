#include "ssim/core/config.hpp"

#include <gtest/gtest.h>

namespace ssim::core {
namespace {

nlohmann::json valid_config_json() {
    return nlohmann::json{
        {"machine", {{"model", "SSIM-128"}, {"softrev", "0.1.0"}}},
        {"wafer",
         {{"diameter_mm", 300},
          {"thickness_um", 775},
          {"film_thickness_um", 1.0},
          {"biaxial_modulus_gpa", 180.5},
          {"truth",
           {{"stress_mpa", -180.0},
            {"initial_curvature_1_per_m", 0.002},
            {"tilt_x_um_per_mm", 0.8},
            {"tilt_y_um_per_mm", -0.3},
            {"anisotropy", 0.05}}},
          {"seed", 12345}}},
        {"scan",
         {{"lines", 6},
          {"points_per_mm", 40},
          {"speed_mm_per_s", 150},
          {"realtime_factor", 1.0},
          {"edge_exclusion_mm", 3.0}}},
        {"noise", {{"sigma_um", 0.5}}},
        {"faults",
         nlohmann::json::array(
             {{{"wafer", "W005"}, {"type", "spike"}, {"rate", 0.002}, {"amplitude_um", 40}}})},
        {"analysis",
         {{"outlier_mad_k", 6.0},
          {"outlier_fraction_alarm", 0.01},
          {"fit_rms_limit_um", 2.0},
          {"stress_spec_mpa", {-400, 400}},
          {"stress_plausible_mpa", {-5000, 5000}},
          {"map_grid_mm", 1.0},
          {"threads", 0}}},
        {"cassette", {{"id", "C001"}, {"slots", 25}}},
        {"comm",
         {{"enabled", true},
          {"bind", "127.0.0.1"},
          {"port", 5000},
          {"device_id", 0},
          {"allow_host_online", true},
          {"t3_s", 45},
          {"t5_s", 10},
          {"t6_s", 5},
          {"t7_s", 10},
          {"t8_s", 5},
          {"linktest_s", 60},
          {"max_frame_bytes", 1048576}}},
        {"output", {{"dir", "results"}, {"save_samples", true}, {"sample_decimation", 10}}},
    };
}

TEST(Config, LoadsAValidFileCorrectly) {
    auto result = load_config_from_json(valid_config_json());
    ASSERT_TRUE(result);
    EXPECT_TRUE(result.value().warnings.empty());

    const Config& cfg = result.value().config;
    EXPECT_EQ(cfg.machine.model, "SSIM-128");
    EXPECT_DOUBLE_EQ(cfg.wafer.diameter_mm, 300.0);
    EXPECT_DOUBLE_EQ(cfg.wafer.truth.stress_mpa, -180.0);
    EXPECT_EQ(cfg.scan.lines, 6);
    EXPECT_EQ(cfg.faults.size(), 1u);
    EXPECT_EQ(cfg.faults[0].wafer, "W005");
    EXPECT_DOUBLE_EQ(cfg.analysis.stress_spec_low_mpa, -400.0);
    EXPECT_DOUBLE_EQ(cfg.analysis.stress_spec_high_mpa, 400.0);
    EXPECT_EQ(cfg.comm.port, 5000);
}

TEST(Config, MissingKeysUseDocumentedDefaults) {
    auto result = load_config_from_json(nlohmann::json::object());
    ASSERT_TRUE(result);

    const Config& cfg = result.value().config;
    const Config defaults{};
    EXPECT_EQ(cfg.machine.model, defaults.machine.model);
    EXPECT_DOUBLE_EQ(cfg.wafer.diameter_mm, defaults.wafer.diameter_mm);
    EXPECT_DOUBLE_EQ(cfg.wafer.truth.stress_mpa, defaults.wafer.truth.stress_mpa);
    EXPECT_EQ(cfg.scan.lines, defaults.scan.lines);
    EXPECT_EQ(cfg.scan.points_per_mm, defaults.scan.points_per_mm);
    EXPECT_TRUE(cfg.faults.empty());
    EXPECT_EQ(cfg.comm.port, defaults.comm.port);
}

TEST(Config, OutOfRangeValueAbortsWithExitCode2) {
    auto raw = valid_config_json();
    raw["scan"]["lines"] = 999;  // FR-CFG-2: 1 to 32

    auto result = load_config_from_json(raw);

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, kConfigErrorExitCode);
    EXPECT_EQ(kConfigErrorExitCode, 2);
}

TEST(Config, PointsPerMmOutOfRangeAborts) {
    auto raw = valid_config_json();
    raw["scan"]["points_per_mm"] = 5;  // valid range is 10 to 80

    auto result = load_config_from_json(raw);

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, kConfigErrorExitCode);
}

TEST(Config, EdgeExclusionOutOfRangeAborts) {
    auto raw = valid_config_json();
    raw["scan"]["edge_exclusion_mm"] = 25.0;  // valid range is 0 to 20

    auto result = load_config_from_json(raw);

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, kConfigErrorExitCode);
}

TEST(Config, UnsupportedWaferDiameterAborts) {
    auto raw = valid_config_json();
    raw["wafer"]["diameter_mm"] = 175;  // only 100/150/200/300 are defined (FR-SCN-1)

    auto result = load_config_from_json(raw);

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, kConfigErrorExitCode);
}

TEST(Config, UnknownKeyWarnsButDoesNotAbort) {
    auto raw = valid_config_json();
    raw["scan"]["totally_unknown_field"] = 1;

    auto result = load_config_from_json(raw);

    ASSERT_TRUE(result);
    ASSERT_EQ(result.value().warnings.size(), 1u);
    EXPECT_NE(result.value().warnings[0].find("totally_unknown_field"), std::string::npos);
}

TEST(Config, UnknownTopLevelSectionWarns) {
    auto raw = valid_config_json();
    raw["not_a_real_section"] = {{"x", 1}};

    auto result = load_config_from_json(raw);

    ASSERT_TRUE(result);
    ASSERT_EQ(result.value().warnings.size(), 1u);
    EXPECT_NE(result.value().warnings[0].find("not_a_real_section"), std::string::npos);
}

}  // namespace
}  // namespace ssim::core
