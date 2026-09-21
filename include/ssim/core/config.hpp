#pragma once

// Thread-safety: Config and its nested structs are plain data; not safe to
// mutate concurrently, safe to share read-only across threads once loaded
// (the intended use: load once at startup, then hand out an immutable
// snapshot — CLAUDE.md §6.2, "other threads read immutable snapshots").
//
// Implements FR-CFG-1/FR-CFG-2 (PRD §7.1, §8.1): parses the JSON sections
// from PRD §8.1 with documented defaults, validates every value against the
// ranges PRD §8.6's equipment-constant table and FR-SCN-1 state explicitly.
// An out-of-range value is an expected failure (CLAUDE.md §6.3): it comes
// back as a Result::err carrying exit code 2, never an exception. An
// unrecognized key is not an error — it is appended to
// ConfigLoadResult::warnings so the caller can log it and continue.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "ssim/core/result.hpp"

namespace ssim::core {

// Startup configuration errors exit with this code (FR-CFG-1, FR-CLI-2).
constexpr int kConfigErrorExitCode = 2;

struct MachineConfig {
    std::string model = "SSIM-128";
    std::string softrev = "0.1.0";
};

struct WaferTruthConfig {
    double stress_mpa = -180.0;
    double initial_curvature_1_per_m = 0.002;
    double tilt_x_um_per_mm = 0.8;
    double tilt_y_um_per_mm = -0.3;
    double anisotropy = 0.05;
};

struct WaferConfig {
    double diameter_mm = 300.0;
    double thickness_um = 775.0;
    double film_thickness_um = 1.0;
    double biaxial_modulus_gpa = 180.5;
    WaferTruthConfig truth;
    std::uint64_t seed = 12345;
};

struct ScanConfig {
    int lines = 6;
    int points_per_mm = 40;
    double speed_mm_per_s = 150.0;
    double realtime_factor = 1.0;
    double edge_exclusion_mm = 3.0;
};

struct NoiseConfig {
    double sigma_um = 0.5;
};

struct FaultConfig {
    std::string wafer;
    std::string type;
    double rate = 0.0;
    double amplitude_um = 0.0;
};

struct AnalysisConfig {
    double outlier_mad_k = 6.0;
    double outlier_fraction_alarm = 0.01;
    double fit_rms_limit_um = 2.0;
    double stress_spec_low_mpa = -400.0;
    double stress_spec_high_mpa = 400.0;
    double stress_plausible_low_mpa = -5000.0;
    double stress_plausible_high_mpa = 5000.0;
    double map_grid_mm = 1.0;
    int threads = 0;
};

struct CassetteConfig {
    std::string id = "C001";
    int slots = 25;
};

struct CommConfig {
    bool enabled = true;
    std::string bind = "127.0.0.1";
    int port = 5000;
    int device_id = 0;
    bool allow_host_online = true;
    double t3_s = 45.0;
    double t5_s = 10.0;
    double t6_s = 5.0;
    double t7_s = 10.0;
    double t8_s = 5.0;
    double linktest_s = 60.0;
    std::size_t max_frame_bytes = 1048576;
};

struct OutputConfig {
    std::string dir = "results";
    bool save_samples = true;
    int sample_decimation = 10;
};

struct Config {
    MachineConfig machine;
    WaferConfig wafer;
    ScanConfig scan;
    NoiseConfig noise;
    std::vector<FaultConfig> faults;
    AnalysisConfig analysis;
    CassetteConfig cassette;
    CommConfig comm;
    OutputConfig output;
};

struct ConfigLoadResult {
    Config config;
    std::vector<std::string> warnings;
};

// Parses and validates an already-loaded JSON document. This is the
// testable core: no filesystem access, so tests build the JSON in memory.
Result<ConfigLoadResult> load_config_from_json(const nlohmann::json& raw);

// Reads path, parses it as JSON and delegates to load_config_from_json. A
// missing or unparsable file is also an expected failure (exit code 2).
Result<ConfigLoadResult> load_config_file(const std::filesystem::path& path);

}  // namespace ssim::core
