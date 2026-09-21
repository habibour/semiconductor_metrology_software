#include "ssim/core/config.hpp"

#include <algorithm>
#include <fstream>
#include <optional>
#include <sstream>

namespace ssim::core {

namespace {

void check_unknown_keys(const nlohmann::json& obj, const std::string& path,
                        const std::vector<std::string>& known_keys,
                        std::vector<std::string>& warnings) {
    if (!obj.is_object()) {
        return;
    }
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        if (std::find(known_keys.begin(), known_keys.end(), it.key()) == known_keys.end()) {
            warnings.push_back("unknown key: " + path + "." + it.key());
        }
    }
}

template <typename T>
T get_or(const nlohmann::json& obj, const std::string& key, T default_value) {
    if (obj.contains(key)) {
        return obj.at(key).get<T>();
    }
    return default_value;
}

Error range_error(const std::string& field, double value, double lo, double hi) {
    std::ostringstream msg;
    msg << field << " = " << value << " is out of range [" << lo << ", " << hi << "]";
    return Error{kConfigErrorExitCode, msg.str()};
}

// FR-CFG-2: validates every value against a stated range. The three ranges
// PRD §8.6 spells out verbatim (edge exclusion, scan lines, points/mm) are
// marked; the rest are sane engineering bounds for values the PRD does not
// give an explicit numeric range for.
std::optional<Error> validate(const Config& cfg) {
    // PRD §8.6 ECID 5002.
    if (cfg.scan.lines < 1 || cfg.scan.lines > 32) {
        return range_error("scan.lines", cfg.scan.lines, 1, 32);
    }
    // PRD §8.6 ECID 5006.
    if (cfg.scan.points_per_mm < 10 || cfg.scan.points_per_mm > 80) {
        return range_error("scan.points_per_mm", cfg.scan.points_per_mm, 10, 80);
    }
    // PRD §8.6 ECID 5001.
    if (cfg.scan.edge_exclusion_mm < 0.0 || cfg.scan.edge_exclusion_mm > 20.0) {
        return range_error("scan.edge_exclusion_mm", cfg.scan.edge_exclusion_mm, 0.0, 20.0);
    }
    if (cfg.scan.realtime_factor < 0.0) {
        return Error{kConfigErrorExitCode, "scan.realtime_factor must be >= 0"};
    }
    if (cfg.scan.speed_mm_per_s <= 0.0) {
        return Error{kConfigErrorExitCode, "scan.speed_mm_per_s must be > 0"};
    }

    // FR-SCN-1: only these four diameters are defined.
    if (cfg.wafer.diameter_mm != 100.0 && cfg.wafer.diameter_mm != 150.0 &&
        cfg.wafer.diameter_mm != 200.0 && cfg.wafer.diameter_mm != 300.0) {
        return Error{kConfigErrorExitCode,
                     "wafer.diameter_mm must be one of 100, 150, 200, 300 (FR-SCN-1)"};
    }
    if (cfg.wafer.thickness_um <= 0.0) {
        return Error{kConfigErrorExitCode, "wafer.thickness_um must be > 0"};
    }
    if (cfg.wafer.film_thickness_um <= 0.0) {
        return Error{kConfigErrorExitCode, "wafer.film_thickness_um must be > 0"};
    }
    if (cfg.wafer.biaxial_modulus_gpa <= 0.0) {
        return Error{kConfigErrorExitCode, "wafer.biaxial_modulus_gpa must be > 0"};
    }

    if (cfg.noise.sigma_um < 0.0) {
        return Error{kConfigErrorExitCode, "noise.sigma_um must be >= 0"};
    }

    // PRD §8.6 ECID 5005.
    if (cfg.analysis.fit_rms_limit_um < 0.1 || cfg.analysis.fit_rms_limit_um > 50.0) {
        return range_error("analysis.fit_rms_limit_um", cfg.analysis.fit_rms_limit_um, 0.1, 50.0);
    }
    // PRD §8.6 ECID 5003/5004.
    if (cfg.analysis.stress_spec_low_mpa < -5000.0 || cfg.analysis.stress_spec_low_mpa > 5000.0) {
        return range_error("analysis.stress_spec_mpa[0]", cfg.analysis.stress_spec_low_mpa, -5000.0,
                           5000.0);
    }
    if (cfg.analysis.stress_spec_high_mpa < -5000.0 || cfg.analysis.stress_spec_high_mpa > 5000.0) {
        return range_error("analysis.stress_spec_mpa[1]", cfg.analysis.stress_spec_high_mpa,
                           -5000.0, 5000.0);
    }
    if (cfg.analysis.stress_spec_low_mpa > cfg.analysis.stress_spec_high_mpa) {
        return Error{kConfigErrorExitCode, "analysis.stress_spec_mpa low must not exceed high"};
    }
    if (cfg.analysis.outlier_mad_k <= 0.0) {
        return Error{kConfigErrorExitCode, "analysis.outlier_mad_k must be > 0"};
    }
    if (cfg.analysis.outlier_fraction_alarm < 0.0 || cfg.analysis.outlier_fraction_alarm > 1.0) {
        return range_error("analysis.outlier_fraction_alarm", cfg.analysis.outlier_fraction_alarm,
                           0.0, 1.0);
    }
    if (cfg.analysis.map_grid_mm <= 0.0) {
        return Error{kConfigErrorExitCode, "analysis.map_grid_mm must be > 0"};
    }
    if (cfg.analysis.threads < 0) {
        return Error{kConfigErrorExitCode, "analysis.threads must be >= 0 (0 means automatic)"};
    }

    // FR-HW-6.
    if (cfg.cassette.slots < 1 || cfg.cassette.slots > 25) {
        return range_error("cassette.slots", cfg.cassette.slots, 1, 25);
    }

    if (cfg.comm.port < 1 || cfg.comm.port > 65535) {
        return range_error("comm.port", cfg.comm.port, 1, 65535);
    }
    if (cfg.comm.t3_s <= 0.0 || cfg.comm.t5_s <= 0.0 || cfg.comm.t6_s <= 0.0 ||
        cfg.comm.t7_s <= 0.0 || cfg.comm.t8_s <= 0.0 || cfg.comm.linktest_s <= 0.0) {
        return Error{kConfigErrorExitCode,
                     "comm timers (t3_s, t5_s, t6_s, t7_s, t8_s, linktest_s) must be > 0"};
    }
    if (cfg.comm.max_frame_bytes == 0) {
        return Error{kConfigErrorExitCode, "comm.max_frame_bytes must be > 0"};
    }

    if (cfg.output.sample_decimation < 1) {
        return Error{kConfigErrorExitCode, "output.sample_decimation must be >= 1"};
    }

    return std::nullopt;
}

}  // namespace

Result<ConfigLoadResult> load_config_from_json(const nlohmann::json& raw) {
    if (!raw.is_object()) {
        return Result<ConfigLoadResult>::err(
            {kConfigErrorExitCode, "config root must be a JSON object"});
    }

    ConfigLoadResult result;
    Config& cfg = result.config;
    std::vector<std::string>& warnings = result.warnings;

    check_unknown_keys(
        raw, "",
        {"machine", "wafer", "scan", "noise", "faults", "analysis", "cassette", "comm", "output"},
        warnings);

    try {
        if (raw.contains("machine")) {
            const auto& m = raw.at("machine");
            check_unknown_keys(m, "machine", {"model", "softrev"}, warnings);
            cfg.machine.model = get_or(m, "model", cfg.machine.model);
            cfg.machine.softrev = get_or(m, "softrev", cfg.machine.softrev);
        }

        if (raw.contains("wafer")) {
            const auto& w = raw.at("wafer");
            check_unknown_keys(w, "wafer",
                               {"diameter_mm", "thickness_um", "film_thickness_um",
                                "biaxial_modulus_gpa", "truth", "seed"},
                               warnings);
            cfg.wafer.diameter_mm = get_or(w, "diameter_mm", cfg.wafer.diameter_mm);
            cfg.wafer.thickness_um = get_or(w, "thickness_um", cfg.wafer.thickness_um);
            cfg.wafer.film_thickness_um =
                get_or(w, "film_thickness_um", cfg.wafer.film_thickness_um);
            cfg.wafer.biaxial_modulus_gpa =
                get_or(w, "biaxial_modulus_gpa", cfg.wafer.biaxial_modulus_gpa);
            cfg.wafer.seed = get_or(w, "seed", cfg.wafer.seed);
            if (w.contains("truth")) {
                const auto& t = w.at("truth");
                check_unknown_keys(t, "wafer.truth",
                                   {"stress_mpa", "initial_curvature_1_per_m", "tilt_x_um_per_mm",
                                    "tilt_y_um_per_mm", "anisotropy"},
                                   warnings);
                cfg.wafer.truth.stress_mpa = get_or(t, "stress_mpa", cfg.wafer.truth.stress_mpa);
                cfg.wafer.truth.initial_curvature_1_per_m = get_or(
                    t, "initial_curvature_1_per_m", cfg.wafer.truth.initial_curvature_1_per_m);
                cfg.wafer.truth.tilt_x_um_per_mm =
                    get_or(t, "tilt_x_um_per_mm", cfg.wafer.truth.tilt_x_um_per_mm);
                cfg.wafer.truth.tilt_y_um_per_mm =
                    get_or(t, "tilt_y_um_per_mm", cfg.wafer.truth.tilt_y_um_per_mm);
                cfg.wafer.truth.anisotropy = get_or(t, "anisotropy", cfg.wafer.truth.anisotropy);
            }
        }

        if (raw.contains("scan")) {
            const auto& s = raw.at("scan");
            check_unknown_keys(s, "scan",
                               {"lines", "points_per_mm", "speed_mm_per_s", "realtime_factor",
                                "edge_exclusion_mm"},
                               warnings);
            cfg.scan.lines = get_or(s, "lines", cfg.scan.lines);
            cfg.scan.points_per_mm = get_or(s, "points_per_mm", cfg.scan.points_per_mm);
            cfg.scan.speed_mm_per_s = get_or(s, "speed_mm_per_s", cfg.scan.speed_mm_per_s);
            cfg.scan.realtime_factor = get_or(s, "realtime_factor", cfg.scan.realtime_factor);
            cfg.scan.edge_exclusion_mm = get_or(s, "edge_exclusion_mm", cfg.scan.edge_exclusion_mm);
        }

        if (raw.contains("noise")) {
            const auto& n = raw.at("noise");
            check_unknown_keys(n, "noise", {"sigma_um"}, warnings);
            cfg.noise.sigma_um = get_or(n, "sigma_um", cfg.noise.sigma_um);
        }

        if (raw.contains("faults")) {
            const auto& f = raw.at("faults");
            if (!f.is_array()) {
                return Result<ConfigLoadResult>::err(
                    {kConfigErrorExitCode, "faults must be an array"});
            }
            for (const auto& item : f) {
                check_unknown_keys(item, "faults[]", {"wafer", "type", "rate", "amplitude_um"},
                                   warnings);
                FaultConfig fc;
                fc.wafer = get_or(item, "wafer", fc.wafer);
                fc.type = get_or(item, "type", fc.type);
                fc.rate = get_or(item, "rate", fc.rate);
                fc.amplitude_um = get_or(item, "amplitude_um", fc.amplitude_um);
                cfg.faults.push_back(std::move(fc));
            }
        }

        if (raw.contains("analysis")) {
            const auto& a = raw.at("analysis");
            check_unknown_keys(
                a, "analysis",
                {"outlier_mad_k", "outlier_fraction_alarm", "fit_rms_limit_um", "stress_spec_mpa",
                 "stress_plausible_mpa", "map_grid_mm", "threads"},
                warnings);
            cfg.analysis.outlier_mad_k = get_or(a, "outlier_mad_k", cfg.analysis.outlier_mad_k);
            cfg.analysis.outlier_fraction_alarm =
                get_or(a, "outlier_fraction_alarm", cfg.analysis.outlier_fraction_alarm);
            cfg.analysis.fit_rms_limit_um =
                get_or(a, "fit_rms_limit_um", cfg.analysis.fit_rms_limit_um);
            if (a.contains("stress_spec_mpa")) {
                const auto& pair = a.at("stress_spec_mpa");
                if (!pair.is_array() || pair.size() != 2) {
                    return Result<ConfigLoadResult>::err(
                        {kConfigErrorExitCode,
                         "analysis.stress_spec_mpa must be a [low, high] array"});
                }
                cfg.analysis.stress_spec_low_mpa = pair.at(0).get<double>();
                cfg.analysis.stress_spec_high_mpa = pair.at(1).get<double>();
            }
            if (a.contains("stress_plausible_mpa")) {
                const auto& pair = a.at("stress_plausible_mpa");
                if (!pair.is_array() || pair.size() != 2) {
                    return Result<ConfigLoadResult>::err(
                        {kConfigErrorExitCode,
                         "analysis.stress_plausible_mpa must be a [low, high] array"});
                }
                cfg.analysis.stress_plausible_low_mpa = pair.at(0).get<double>();
                cfg.analysis.stress_plausible_high_mpa = pair.at(1).get<double>();
            }
            cfg.analysis.map_grid_mm = get_or(a, "map_grid_mm", cfg.analysis.map_grid_mm);
            cfg.analysis.threads = get_or(a, "threads", cfg.analysis.threads);
        }

        if (raw.contains("cassette")) {
            const auto& c = raw.at("cassette");
            check_unknown_keys(c, "cassette", {"id", "slots"}, warnings);
            cfg.cassette.id = get_or(c, "id", cfg.cassette.id);
            cfg.cassette.slots = get_or(c, "slots", cfg.cassette.slots);
        }

        if (raw.contains("comm")) {
            const auto& c = raw.at("comm");
            check_unknown_keys(c, "comm",
                               {"enabled", "bind", "port", "device_id", "allow_host_online", "t3_s",
                                "t5_s", "t6_s", "t7_s", "t8_s", "linktest_s", "max_frame_bytes"},
                               warnings);
            cfg.comm.enabled = get_or(c, "enabled", cfg.comm.enabled);
            cfg.comm.bind = get_or(c, "bind", cfg.comm.bind);
            cfg.comm.port = get_or(c, "port", cfg.comm.port);
            cfg.comm.device_id = get_or(c, "device_id", cfg.comm.device_id);
            cfg.comm.allow_host_online = get_or(c, "allow_host_online", cfg.comm.allow_host_online);
            cfg.comm.t3_s = get_or(c, "t3_s", cfg.comm.t3_s);
            cfg.comm.t5_s = get_or(c, "t5_s", cfg.comm.t5_s);
            cfg.comm.t6_s = get_or(c, "t6_s", cfg.comm.t6_s);
            cfg.comm.t7_s = get_or(c, "t7_s", cfg.comm.t7_s);
            cfg.comm.t8_s = get_or(c, "t8_s", cfg.comm.t8_s);
            cfg.comm.linktest_s = get_or(c, "linktest_s", cfg.comm.linktest_s);
            cfg.comm.max_frame_bytes = get_or(c, "max_frame_bytes", cfg.comm.max_frame_bytes);
        }

        if (raw.contains("output")) {
            const auto& o = raw.at("output");
            check_unknown_keys(o, "output", {"dir", "save_samples", "sample_decimation"}, warnings);
            cfg.output.dir = get_or(o, "dir", cfg.output.dir);
            cfg.output.save_samples = get_or(o, "save_samples", cfg.output.save_samples);
            cfg.output.sample_decimation =
                get_or(o, "sample_decimation", cfg.output.sample_decimation);
        }
    } catch (const nlohmann::json::exception& e) {
        return Result<ConfigLoadResult>::err(
            {kConfigErrorExitCode, std::string("config parse error: ") + e.what()});
    }

    if (auto err = validate(cfg)) {
        return Result<ConfigLoadResult>::err(*err);
    }

    return Result<ConfigLoadResult>::ok(std::move(result));
}

Result<ConfigLoadResult> load_config_file(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in) {
        return Result<ConfigLoadResult>::err(
            {kConfigErrorExitCode, "cannot open config file: " + path.string()});
    }
    nlohmann::json raw;
    try {
        in >> raw;
    } catch (const nlohmann::json::parse_error& e) {
        return Result<ConfigLoadResult>::err(
            {kConfigErrorExitCode, std::string("config JSON parse error: ") + e.what()});
    }
    return load_config_from_json(raw);
}

}  // namespace ssim::core
