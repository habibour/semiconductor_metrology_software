#pragma once

// Thread-safety: every type here is a plain value type — safe to copy or
// move across threads (e.g. into the fit thread pool's per-line jobs).
//
// PRD §8.4's ten-step pipeline operates line by line; these are the shapes
// that flow between its stages (edge exclusion -> outlier rejection -> line
// fit -> combine -> stoney -> uncertainty -> quality gates -> wafer map).

#include <cstddef>
#include <vector>

namespace ssim::analysis {

// FR-CLI-2: exit code 3 is "runtime fault" — a writer failing to create an
// output file (already exists, permission denied, disk full) is exactly
// that, not a configuration error (2).
constexpr int kWriteErrorExitCode = 3;

// One line's raw scan data: position along the line (metres, signed,
// relative to the wafer centre) and measured height (metres). A NaN height
// marks a sample the scan thread never received (FaultType::kDropout).
struct LineSamples {
    double angle_rad = 0.0;
    std::vector<double> s_m;
    std::vector<double> z_m;
};

// PRD §8.5 sample CSV "flag" column: 0 kept, 1 edge-excluded, 2 outlier.
// kDropped (a dropout gap) is reported as its own value since it never had
// a height to plot in the first place.
enum class SampleFlag { kKept = 0, kEdgeExcluded = 1, kOutlier = 2, kDropped = 3 };

struct FilteredSample {
    double s_m = 0.0;
    double z_m = 0.0;  // meaningless (not read) unless flag == kKept
    SampleFlag flag = SampleFlag::kKept;
};

struct FilterResult {
    double angle_rad = 0.0;
    std::vector<FilteredSample> samples;  // every input sample, each tagged
    std::size_t removed_count = 0;        // edge-excluded + outlier + dropped
    double removed_fraction = 0.0;        // removed_count / samples.size()
};

struct LineFitResult {
    double angle_rad = 0.0;
    double a = 0.0;  // z = a*s^2 + b*s + c
    double b = 0.0;
    double c = 0.0;
    double curvature_per_m = 0.0;     // 2*a
    double se_curvature_per_m = 0.0;  // 2 * standard error of a
    double residual_rms_m = 0.0;
    std::size_t n_used = 0;
    bool valid = false;  // false if too few points to fit (n_used < 3)
};

}  // namespace ssim::analysis
