#include "ssim/analysis/writers/png_writer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

// This is the one translation unit that owns stb_image_write's
// implementation (header-only library, PRD §6.10 allowed dependency).
// stb's own code trips this project's -Wall -Wextra -Wpedantic -Werror
// policy (missing-field-initializers, a deprecated sprintf); it is
// third-party code we don't control, so the diagnostics are suppressed
// only around this one include, not for our own code.
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace ssim::analysis {

namespace {
constexpr int kColorBarWidth = 20;
constexpr int kMargin = 10;

// Diverging blue -> white -> red, t in [0, 1].
std::array<unsigned char, 3> diverging_color(double t) {
    t = std::clamp(t, 0.0, 1.0);
    double r, g, b;
    if (t < 0.5) {
        const double u = t * 2.0;
        r = u;
        g = u;
        b = 1.0;
    } else {
        const double u = (t - 0.5) * 2.0;
        r = 1.0;
        g = 1.0 - u;
        b = 1.0 - u;
    }
    return {static_cast<unsigned char>(std::lround(r * 255.0)),
           static_cast<unsigned char>(std::lround(g * 255.0)),
           static_cast<unsigned char>(std::lround(b * 255.0))};
}

void put_pixel(std::vector<unsigned char>& pixels, int stride, int x, int y,
              std::array<unsigned char, 3> rgb) {
    const std::size_t idx = static_cast<std::size_t>(y) * static_cast<std::size_t>(stride) +
                            static_cast<std::size_t>(x) * 3;
    pixels[idx] = rgb[0];
    pixels[idx + 1] = rgb[1];
    pixels[idx + 2] = rgb[2];
}
}  // namespace

ssim::core::Result<std::filesystem::path> write_wafer_map_png(const std::filesystem::path& wafer_dir,
                                                               const WaferMap& map,
                                                               double edge_exclusion_mm) {
    const std::filesystem::path path = wafer_dir / "wafer_map.png";
    if (std::filesystem::exists(path)) {
        return ssim::core::Result<std::filesystem::path>::err(
            {kWriteErrorExitCode, "wafer_map.png already exists: " + path.string()});
    }

    double lo = std::numeric_limits<double>::infinity();
    double hi = -std::numeric_limits<double>::infinity();
    for (double v : map.heights_m) {
        if (std::isfinite(v)) {
            lo = std::min(lo, v);
            hi = std::max(hi, v);
        }
    }
    const bool has_data = lo <= hi;
    if (!has_data) {
        lo = 0.0;
        hi = 1.0;
    }
    const double span = (hi > lo) ? (hi - lo) : 1.0;

    const int width = map.width + kMargin + kColorBarWidth;
    const int height = std::max(map.height, 1);
    const int stride = width * 3;
    std::vector<unsigned char> pixels(static_cast<std::size_t>(stride) *
                                      static_cast<std::size_t>(height),
                                      /*background=*/200);

    const double radius_mm = map.diameter_m * 1000.0 / 2.0;
    const double half_n = (map.width - 1) / 2.0;
    for (int j = 0; j < map.height; ++j) {
        for (int i = 0; i < map.width; ++i) {
            const std::size_t idx =
                static_cast<std::size_t>(j) * static_cast<std::size_t>(map.width) +
                static_cast<std::size_t>(i);
            const double v = map.heights_m[idx];
            if (!std::isfinite(v)) {
                continue;  // leave the neutral background
            }
            auto rgb = diverging_color((v - lo) / span);

            // Edge-exclusion ring: darken pixels within half a grid cell of
            // the boundary radius - edge_exclusion.
            const double x_mm = (i - half_n) * map.grid_mm;
            const double y_mm = (j - half_n) * map.grid_mm;
            const double r_mm = std::hypot(x_mm, y_mm);
            const double ring_r = radius_mm - edge_exclusion_mm;
            if (std::abs(r_mm - ring_r) < map.grid_mm * 0.5) {
                rgb = {0, 0, 0};
            }
            put_pixel(pixels, stride, i, j, rgb);
        }
    }

    // Colour bar: right-hand strip, top = hi, bottom = lo.
    for (int j = 0; j < height; ++j) {
        const double t = 1.0 - static_cast<double>(j) / static_cast<double>(std::max(height - 1, 1));
        const auto rgb = diverging_color(t);
        for (int i = 0; i < kColorBarWidth; ++i) {
            put_pixel(pixels, stride, map.width + kMargin + i, j, rgb);
        }
    }

    const int ok = stbi_write_png(path.string().c_str(), width, height, 3, pixels.data(), stride);
    if (ok == 0) {
        return ssim::core::Result<std::filesystem::path>::err(
            {kWriteErrorExitCode, "stbi_write_png failed: " + path.string()});
    }
    return ssim::core::Result<std::filesystem::path>::ok(path);
}

}  // namespace ssim::analysis
