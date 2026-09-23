#include "ssim/analysis/line_fit.hpp"

#include <cmath>

namespace ssim::analysis {

namespace {
// Solves the 3x3 symmetric normal-equations system for a quadratic
// least-squares fit via Cramer's rule; also returns the (0,0) cofactor of M
// (= det of the submatrix formed by dropping row/col 0), needed separately
// to get Var(a) = sigma^2 * cofactor00 / det(M) without inverting M.
struct NormalEquations {
    double m[3][3];  // symmetric: rows/cols are [s^2, s, 1]
    double rhs[3];
};

double det3(const double m[3][3]) {
    return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) -
          m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
          m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
}

// Cramer's rule: replaces column `col` of m with rhs and returns det/det(m).
double cramer_solve(const double m[3][3], const double rhs[3], int col, double det_m) {
    double sub[3][3];
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            sub[r][c] = (c == col) ? rhs[r] : m[r][c];
        }
    }
    return det3(sub) / det_m;
}
}  // namespace

LineFitResult fit_line(const FilterResult& filtered) {
    LineFitResult result;
    result.angle_rad = filtered.angle_rad;

    double s0 = 0.0, s1 = 0.0, s2 = 0.0, s3 = 0.0, s4 = 0.0;
    double t0 = 0.0, t1 = 0.0, t2 = 0.0;
    std::size_t n = 0;
    for (const auto& sample : filtered.samples) {
        if (sample.flag != SampleFlag::kKept) {
            continue;
        }
        const double s = sample.s_m;
        const double z = sample.z_m;
        const double s_sq = s * s;
        s0 += 1.0;
        s1 += s;
        s2 += s_sq;
        s3 += s_sq * s;
        s4 += s_sq * s_sq;
        t0 += z;
        t1 += s * z;
        t2 += s_sq * z;
        ++n;
    }
    result.n_used = n;
    if (n < 3) {
        result.valid = false;
        return result;
    }

    const double m[3][3] = {
        {s4, s3, s2},
        {s3, s2, s1},
        {s2, s1, s0},
    };
    const double rhs[3] = {t2, t1, t0};
    const double det_m = det3(m);
    if (det_m == 0.0) {
        result.valid = false;
        return result;
    }

    result.a = cramer_solve(m, rhs, 0, det_m);
    result.b = cramer_solve(m, rhs, 1, det_m);
    result.c = cramer_solve(m, rhs, 2, det_m);
    result.curvature_per_m = 2.0 * result.a;

    double rss = 0.0;
    for (const auto& sample : filtered.samples) {
        if (sample.flag != SampleFlag::kKept) {
            continue;
        }
        const double s = sample.s_m;
        const double predicted = result.a * s * s + result.b * s + result.c;
        const double residual = sample.z_m - predicted;
        rss += residual * residual;
    }
    result.residual_rms_m = std::sqrt(rss / static_cast<double>(n));

    if (n > 3) {
        const double sigma_sq = rss / static_cast<double>(n - 3);
        // Var(a) = sigma^2 * cofactor00(M) / det(M); cofactor00 is the
        // determinant of M with row/col 0 removed: |s2 s1; s1 s0|.
        const double cofactor00 = s2 * s0 - s1 * s1;
        const double var_a = sigma_sq * cofactor00 / det_m;
        result.se_curvature_per_m = 2.0 * std::sqrt(var_a > 0.0 ? var_a : 0.0);
    }

    result.valid = true;
    return result;
}

}  // namespace ssim::analysis
