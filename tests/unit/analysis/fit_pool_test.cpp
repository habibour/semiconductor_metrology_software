#include "ssim/analysis/fit_pool.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <random>
#include <vector>

#include "ssim/analysis/line_fit.hpp"
#include "ssim/analysis/pipeline.hpp"
#include "ssim/core/config.hpp"
#include "ssim/core/scan_types.hpp"

namespace ssim::analysis {
namespace {

FilterResult make_line(double a, double b, double c, std::uint64_t seed) {
    FilterResult result;
    std::mt19937_64 rng(seed);
    std::normal_distribution<double> noise(0.0, 0.5e-6);
    for (int i = 0; i < 41; ++i) {
        const double s = -0.15 + 0.3 * static_cast<double>(i) / 40.0;
        const double z = a * s * s + b * s + c + noise(rng);
        result.samples.push_back(FilteredSample{s, z, SampleFlag::kKept});
    }
    return result;
}

// FR-PRC-9: results from the pool must be bit-for-bit identical to fitting
// the same lines serially, regardless of how many worker threads ran them.
TEST(FitThreadPool, MatchesSerialFitBitForBit) {
    std::vector<FilterResult> lines;
    for (int i = 0; i < 8; ++i) {
        lines.push_back(make_line(-0.005 + 0.0001 * i, 0.001 * i, 1e-6 * i,
                                  /*seed=*/static_cast<std::uint64_t>(1000 + i)));
    }

    std::vector<LineFitResult> serial;
    for (const auto& line : lines) {
        serial.push_back(fit_line(line));
    }

    FitThreadPool pool(4);
    auto pooled = pool.fit_lines(lines);

    ASSERT_EQ(pooled.size(), serial.size());
    for (std::size_t i = 0; i < serial.size(); ++i) {
        EXPECT_EQ(pooled[i].a, serial[i].a);
        EXPECT_EQ(pooled[i].b, serial[i].b);
        EXPECT_EQ(pooled[i].c, serial[i].c);
        EXPECT_EQ(pooled[i].curvature_per_m, serial[i].curvature_per_m);
        EXPECT_EQ(pooled[i].residual_rms_m, serial[i].residual_rms_m);
    }
}

TEST(FitThreadPool, HandlesEmptyInput) {
    FitThreadPool pool(2);
    auto results = pool.fit_lines({});
    EXPECT_TRUE(results.empty());
}

TEST(FitThreadPool, ZeroThreadsSelectsAutomaticCount) {
    FitThreadPool pool(0);
    std::vector<FilterResult> lines = {make_line(-0.005, 0.0, 0.0, 1)};
    auto results = pool.fit_lines(lines);
    ASSERT_EQ(results.size(), 1u);
    EXPECT_TRUE(results[0].valid);
}

TEST(FitThreadPool, ForEachIndexRunsEveryIndexExactlyOnce) {
    FitThreadPool pool(4);
    std::vector<std::atomic<int>> hits(200);
    pool.for_each_index(hits.size(), [&hits](std::size_t i) { ++hits[i]; });
    for (const auto& h : hits) {
        EXPECT_EQ(h.load(), 1);
    }
}

TEST(FitThreadPool, ForEachIndexWithZeroCountReturnsImmediately) {
    FitThreadPool pool(2);
    pool.for_each_index(0, [](std::size_t) { FAIL() << "must not be called"; });
}

std::vector<ssim::core::SampleBlock> make_blocks() {
    std::mt19937_64 rng(7);
    std::normal_distribution<double> noise(0.0, 0.5e-6);
    std::uniform_real_distribution<double> unit(0.0, 1.0);
    std::vector<ssim::core::SampleBlock> blocks;
    for (int line = 0; line < 6; ++line) {
        ssim::core::SampleBlock block;
        block.line_index = static_cast<std::size_t>(line);
        block.angle_rad = line * 0.5236;
        for (int i = 0; i < 3000; ++i) {
            const double s = -0.15 + 0.3 * i / 2999.0;
            double z = -0.004 * s * s + 0.0005 * s + noise(rng);
            if (unit(rng) < 0.01) {
                z += 30e-6;  // a spike for the outlier filter to find
            }
            block.positions_m.push_back(s);
            block.heights_m.push_back(z);
        }
        blocks.push_back(std::move(block));
    }
    return blocks;
}

// The per-line filter stage runs on the pool, so the answer must not depend on
// how many workers there are or which one got which line.
TEST(FitThreadPool, PipelineResultDoesNotDependOnThreadCount) {
    const ssim::core::Config config;
    const auto blocks = make_blocks();

    FitThreadPool one(1);
    const PipelineResult reference = run_pipeline(blocks, config, one);
    std::size_t outliers = 0;
    for (const auto& line : reference.filtered_lines) {
        for (const auto& sample : line.samples) {
            outliers += sample.flag == SampleFlag::kOutlier ? 1 : 0;
        }
    }
    ASSERT_GT(outliers, 0u) << "the test data must exercise the outlier filter";

    for (std::size_t threads : {2u, 3u, 6u}) {
        FitThreadPool pool(threads);
        const PipelineResult other = run_pipeline(blocks, config, pool);
        EXPECT_EQ(other.stress_pa, reference.stress_pa) << threads;
        EXPECT_EQ(other.removed_fraction_overall, reference.removed_fraction_overall) << threads;
        EXPECT_EQ(other.outlier_removed_fraction, reference.outlier_removed_fraction) << threads;
        ASSERT_EQ(other.filtered_lines.size(), reference.filtered_lines.size());
        for (std::size_t l = 0; l < reference.filtered_lines.size(); ++l) {
            ASSERT_EQ(other.filtered_lines[l].samples.size(),
                      reference.filtered_lines[l].samples.size());
            for (std::size_t i = 0; i < reference.filtered_lines[l].samples.size(); ++i) {
                ASSERT_EQ(static_cast<int>(other.filtered_lines[l].samples[i].flag),
                          static_cast<int>(reference.filtered_lines[l].samples[i].flag));
            }
            EXPECT_EQ(other.line_fits[l].a, reference.line_fits[l].a);
        }
    }
}

}  // namespace
}  // namespace ssim::analysis
