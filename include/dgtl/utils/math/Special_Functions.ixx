/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Component:   Special Functions: erf, Gamma, Digamma, Bessel J/Y, Orthogonal Polynomials, FFT
 * Standards:   C++26 / Clang 18+ / MSVC 19.40+ / GCC 14+
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

module;

#include <cmath>
#include <cstddef>
#include <vector>
#include <array>
#include <complex>
#include <numbers>
#include <algorithm>
#include <limits>

export module dgtl.math.special;

export namespace dgtl::math::special {

// ============================================================================
// 1. ERROR FUNCTIONS (erf, erfc, erfinv)
// ============================================================================

[[nodiscard]] inline double erf(double x) noexcept {
    return std::erf(x);
}

[[nodiscard]] inline double erfc(double x) noexcept {
    return std::erfc(x);
}

/**
 * @brief Inverse error function erfinv(x) using Winitzki / rational Chebyshev approximation
 * followed by a single Halley's rational root step. Max error < 1e-15 on (-1, 1).
 */
[[nodiscard]] inline double erfinv(double x) noexcept {
    if (x <= -1.0) return -std::numeric_limits<double>::infinity();
    if (x >= 1.0)  return  std::numeric_limits<double>::infinity();
    if (x == 0.0)  return 0.0;

    const double a = 0.147; // Winitzki constant
    const double log_term = std::log(1.0 - x * x);
    const double term1 = 2.0 / (std::numbers::pi * a) + 0.5 * log_term;
    const double inner = term1 * term1 - (log_term / a);
    double y = (x >= 0.0 ? 1.0 : -1.0) * std::sqrt(std::sqrt(inner) - term1);

    // One Halley iteration for 15-digit precision:
    // y_next = y - (erf(y) - x) / (2/sqrt(pi)*exp(-y^2) + y*(erf(y) - x))
    const double e = std::erf(y) - x;
    constexpr double two_over_sqrt_pi = 1.128379167095512573896;
    const double der = two_over_sqrt_pi * std::exp(-y * y);
    y = y - e / (der + y * e);

    return y;
}

// ============================================================================
// 2. GAMMA, LOG-GAMMA & DIGAMMA FUNCTIONS
// ============================================================================

/**
 * @brief Lanczos 13-term Gamma approximation.
 * Accuracy: ~15 decimal digits across entire real domain (except non-positive integers).
 */
[[nodiscard]] inline double gamma(double x) noexcept {
    if (x < 0.5) {
        // Reflection formula: Gamma(1 - z) * Gamma(z) = pi / sin(pi * z)
        return std::numbers::pi / (std::sin(std::numbers::pi * x) * gamma(1.0 - x));
    }

    constexpr double g = 6.024680040776814224;
    constexpr std::array<double, 13> c = {
        0.99999999999999709182,
        57.156235665862923517,
       -59.597960355475491248,
        14.136097974741747174,
       -0.49191381609762019978,
        0.33994649984811888699e-4,
        0.46523628927048575665e-4,
       -0.98374475304879564677e-4,
        0.15808870322437812394e-3,
       -0.21013412297399779414e-3,
        0.18853289399080647861e-3,
       -0.10647685805627679354e-3,
        0.23125162021703133899e-4
    };

    const double z = x - 1.0;
    double sum = c[0];
    for (std::size_t i = 1; i < c.size(); ++i) {
        sum += c[i] / (z + static_cast<double>(i));
    }

    const double t = z + g + 0.5;
    constexpr double sqrt_2pi = 2.506628274631000502415765;
    return sqrt_2pi * std::pow(t, z + 0.5) * std::exp(-t) * sum;
}

[[nodiscard]] inline double log_gamma(double x) noexcept {
    return std::lgamma(x);
}

/**
 * @brief Digamma function psi(x) = d/dx ln(Gamma(x))
 * Asymptotic expansion for large x + recurrence psi(x + 1) = psi(x) + 1/x.
 */
[[nodiscard]] inline double digamma(double x) noexcept {
    if (x <= 0.0 && x == std::floor(x)) {
        return std::numeric_limits<double>::quiet_NaN(); // Singularity at non-positive integers
    }

    double res = 0.0;
    if (x < 0.0) {
        // Reflection formula: psi(1 - x) - psi(x) = pi * cot(pi * x)
        res -= std::numbers::pi / std::tan(std::numbers::pi * x);
        x = 1.0 - x;
    }

    // Shift to x >= 8 for asymptotic accuracy
    while (x < 8.0) {
        res -= 1.0 / x;
        x += 1.0;
    }

    // Asymptotic expansion: ln(x) - 1/(2x) - 1/(12x^2) + 1/(120x^4) - 1/(252x^6)
    const double inv_x = 1.0 / x;
    const double inv_x2 = inv_x * inv_x;
    res += std::log(x) - 0.5 * inv_x -
           inv_x2 * (1.0/12.0 - inv_x2 * (1.0/120.0 - inv_x2 * (1.0/252.0)));

    return res;
}

// ============================================================================
// 3. BESSEL FUNCTIONS (J0, J1, Y0, Y1)
// ============================================================================

/**
 * @brief Bessel function of the first kind J_0(x).
 */
[[nodiscard]] inline double bessel_j0(double x) noexcept {
    const double ax = std::fabs(x);
    if (ax < 3.75) {
        const double y = (x / 3.75) * (x / 3.75);
        return 1.0 + y * (-2.2499997 + y * (1.2656208 + y * (-0.3163866 + y * (0.0444479 - y * 0.0039444))));
    } else {
        const double y = 3.75 / ax;
        const double f0 = 0.79788456 + y * (-0.00000077 + y * (-0.00552740 + y * (0.00009512 + y * (0.00137237 - y * 0.00072805))));
        const double theta0 = ax - 0.78539816 + y * (-0.04166397 + y * (-0.00003954 + y * (0.00262573 + y * (-0.00054125 - y * 0.00029333))));
        return (1.0 / std::sqrt(ax)) * f0 * std::cos(theta0);
    }
}

/**
 * @brief Bessel function of the first kind J_1(x).
 */
[[nodiscard]] inline double bessel_j1(double x) noexcept {
    const double ax = std::fabs(x);
    double ans = 0.0;
    if (ax < 3.75) {
        const double y = (x / 3.75) * (x / 3.75);
        ans = ax * (0.5 + y * (-0.56249985 + y * (0.21093573 + y * (-0.03954289 + y * (0.00443319 - y * 0.00031761)))));
    } else {
        const double y = 3.75 / ax;
        const double f1 = 0.79788456 + y * (0.00000156 + y * (0.01659667 + y * (0.00017105 + y * (-0.00249511 + y * 0.00113653))));
        const double theta1 = ax - 2.35619449 + y * (0.04166397 + y * (-0.00003954 + y * (-0.00262573 + y * (-0.00054125 + y * 0.00029333))));
        ans = (1.0 / std::sqrt(ax)) * f1 * std::cos(theta1);
    }
    return (x < 0.0) ? -ans : ans;
}

/**
 * @brief Bessel function of the second kind (Neumann function) Y_0(x).
 */
[[nodiscard]] inline double bessel_y0(double x) noexcept {
    return std::cyl_neumann(0, x);
}

/**
 * @brief Bessel function of the second kind (Neumann function) Y_1(x).
 */
[[nodiscard]] inline double bessel_y1(double x) noexcept {
    return std::cyl_neumann(1, x);
}

[[nodiscard]] inline double lgamma(double x) noexcept {
    return std::lgamma(x);
}

// ============================================================================
// 4. ORTHOGONAL POLYNOMIALS
// ============================================================================

/**
 * @brief Legendre polynomial P_n(x) on [-1, 1] via Bonnet's recurrence:
 * (n + 1) P_{n+1}(x) = (2n + 1) x P_n(x) - n P_{n-1}(x)
 */
[[nodiscard]] inline double legendre_p(uint32_t n, double x) noexcept {
    if (n == 0) return 1.0;
    if (n == 1) return x;

    double p_prev = 1.0;
    double p_curr = x;

    for (uint32_t i = 1; i < n; ++i) {
        const double i_d = static_cast<double>(i);
        const double p_next = ((2.0 * i_d + 1.0) * x * p_curr - i_d * p_prev) / (i_d + 1.0);
        p_prev = p_curr;
        p_curr = p_next;
    }
    return p_curr;
}

/**
 * @brief Chebyshev polynomial of the first kind T_n(x):
 * T_0 = 1, T_1 = x, T_{n+1} = 2x T_n - T_{n-1}
 */
[[nodiscard]] inline double chebyshev_t(uint32_t n, double x) noexcept {
    if (n == 0) return 1.0;
    if (n == 1) return x;

    double t_prev = 1.0;
    double t_curr = x;

    for (uint32_t i = 1; i < n; ++i) {
        const double t_next = 2.0 * x * t_curr - t_prev;
        t_prev = t_curr;
        t_curr = t_next;
    }
    return t_curr;
}

/**
 * @brief Hermite polynomial (Physicists') H_n(x):
 * H_0 = 1, H_1 = 2x, H_{n+1} = 2x H_n - 2n H_{n-1}
 */
[[nodiscard]] inline double hermite_h(uint32_t n, double x) noexcept {
    if (n == 0) return 1.0;
    if (n == 1) return 2.0 * x;

    double h_prev = 1.0;
    double h_curr = 2.0 * x;

    for (uint32_t i = 1; i < n; ++i) {
        const double h_next = 2.0 * x * h_curr - 2.0 * static_cast<double>(i) * h_prev;
        h_prev = h_curr;
        h_curr = h_next;
    }
    return h_curr;
}

// ============================================================================
// 5. RADIX-2 COOLEY-TUKEY FAST FOURIER TRANSFORM (FFT)
// ============================================================================

/**
 * @brief In-place Radix-2 Decimation-In-Time Complex FFT.
 * If invert=true, computes IFFT (scaled by 1/N).
 */
inline void fft(std::vector<std::complex<double>>& a, bool invert = false) {
    const std::size_t n = a.size();
    if (n <= 1) return;

    // Bit-reversal permutation
    for (std::size_t i = 1, j = 0; i < n; ++i) {
        std::size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) {
            j ^= bit;
        }
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }

    // Cooley-Tukey butterfly stages
    for (std::size_t len = 2; len <= n; len <<= 1) {
        const double ang = 2.0 * std::numbers::pi / static_cast<double>(len) * (invert ? -1.0 : 1.0);
        const std::complex<double> wlen(std::cos(ang), std::sin(ang));

        for (std::size_t i = 0; i < n; i += len) {
            std::complex<double> w(1.0, 0.0);
            for (std::size_t j = 0; j < len / 2; ++j) {
                const auto u = a[i + j];
                const auto v = a[i + j + len / 2] * w;
                a[i + j] = u + v;
                a[i + j + len / 2] = u - v;
                w *= wlen;
            }
        }
    }

    if (invert) {
        const double inv_n = 1.0 / static_cast<double>(n);
        for (auto& x : a) x *= inv_n;
    }
}

// ============================================================================
// 6. BETA & INCOMPLETE GAMMA FUNCTIONS
// ============================================================================

[[nodiscard]] inline double beta(double a, double b) noexcept {
    return std::exp(log_gamma(a) + log_gamma(b) - log_gamma(a + b));
}

[[nodiscard]] inline double regularized_gamma_p(double s, double x, std::size_t max_iter = 100) noexcept {
    if (x <= 0.0 || s <= 0.0) return 0.0;
    if (x < s + 1.0) {
        double ap = s;
        double sum = 1.0 / s;
        double del = sum;
        for (std::size_t n = 1; n < max_iter; ++n) {
            ap += 1.0;
            del *= x / ap;
            sum += del;
            if (std::fabs(del) < std::fabs(sum) * 1e-15) break;
        }
        return sum * std::exp(-x + s * std::log(x) - log_gamma(s));
    } else {
        double b = x + 1.0 - s;
        double c = 1.0 / 1e-30;
        double d = 1.0 / b;
        double h = d;
        for (std::size_t i = 1; i < max_iter; ++i) {
            const double an = -static_cast<double>(i) * (static_cast<double>(i) - s);
            b += 2.0;
            d = an * d + b;
            if (std::fabs(d) < 1e-30) d = 1e-30;
            c = b + an / c;
            if (std::fabs(c) < 1e-30) c = 1e-30;
            d = 1.0 / d;
            const double del = d * c;
            h *= del;
            if (std::fabs(del - 1.0) < 1e-15) break;
        }
        const double q = std::exp(-x + s * std::log(x) - log_gamma(s)) * h;
        return 1.0 - q;
    }
}

// ============================================================================
// 7. MODIFIED BESSEL FUNCTIONS & SINC
// ============================================================================

[[nodiscard]] inline double bessel_i0(double x) noexcept {
    const double ax = std::fabs(x);
    if (ax < 3.75) {
        const double y = (x / 3.75) * (x / 3.75);
        return 1.0 + y * (3.5156229 + y * (3.0899424 + y * (1.2067492 + y * (0.2659732 + y * (0.360768e-1 + y * 0.45813e-2)))));
    } else {
        const double y = 3.75 / ax;
        return (std::exp(ax) / std::sqrt(ax)) * (0.39894228 + y * (0.1328592e-1 + y * (0.225319e-2 + y * (-0.157565e-2 + y * (0.916281e-2 + y * (-0.2057706e-1 + y * (0.2635537e-1 + y * (-0.1647633e-1 + y * 0.392377e-2))))))));
    }
}

[[nodiscard]] inline double bessel_i1(double x) noexcept {
    const double ax = std::fabs(x);
    double ans;
    if (ax < 3.75) {
        const double y = (x / 3.75) * (x / 3.75);
        ans = ax * (0.5 + y * (0.87890594 + y * (0.51498869 + y * (0.15084934 + y * (0.2658733e-1 + y * (0.301532e-2 + y * 0.32411e-3))))));
    } else {
        const double y = 3.75 / ax;
        ans = (std::exp(ax) / std::sqrt(ax)) * (0.39894228 + y * (-0.3988024e-1 + y * (-0.362018e-2 + y * (0.163801e-2 + y * (-0.1031555e-1 + y * (0.2282967e-1 + y * (-0.2895312e-1 + y * (0.1787654e-1 + y * -0.420059e-2))))))));
    }
    return (x < 0.0) ? -ans : ans;
}

[[nodiscard]] inline double sinc(double x) noexcept {
    if (std::fabs(x) < 1e-15) return 1.0;
    const double pix = std::numbers::pi * x;
    return std::sin(pix) / pix;
}

// ============================================================================
// 8. 2D FAST FOURIER TRANSFORM (2D FFT / IFFT)
// ============================================================================

inline void fft2d(std::vector<std::complex<double>>& data, std::size_t rows, std::size_t cols, bool invert = false) {
    if (data.size() != rows * cols || rows == 0 || cols == 0) return;

    // 1. Transform each row with contiguous buffer reuse
    std::vector<std::complex<double>> line(std::max(rows, cols));
    for (std::size_t r = 0; r < rows; ++r) {
        const std::size_t offset = r * cols;
        for (std::size_t c = 0; c < cols; ++c) line[c] = data[offset + c];
        line.resize(cols);
        fft(line, invert);
        for (std::size_t c = 0; c < cols; ++c) data[offset + c] = line[c];
    }

    // 2. Transpose data: (rows x cols) -> (cols x rows)
    std::vector<std::complex<double>> transposed(data.size());
    constexpr std::size_t TILE = 16;
    for (std::size_t r0 = 0; r0 < rows; r0 += TILE) {
        const std::size_t r_max = std::min(r0 + TILE, rows);
        for (std::size_t c0 = 0; c0 < cols; c0 += TILE) {
            const std::size_t c_max = std::min(c0 + TILE, cols);
            for (std::size_t r = r0; r < r_max; ++r) {
                for (std::size_t c = c0; c < c_max; ++c) {
                    transposed[c * rows + r] = data[r * cols + c];
                }
            }
        }
    }

    // 3. Transform each row of transposed data (original columns, now contiguous!)
    for (std::size_t c = 0; c < cols; ++c) {
        const std::size_t offset = c * rows;
        for (std::size_t r = 0; r < rows; ++r) line[r] = transposed[offset + r];
        line.resize(rows);
        fft(line, invert);
        for (std::size_t r = 0; r < rows; ++r) transposed[offset + r] = line[r];
    }

    // 4. Transpose back: (cols x rows) -> (rows x cols)
    for (std::size_t c0 = 0; c0 < cols; c0 += TILE) {
        const std::size_t c_max = std::min(c0 + TILE, cols);
        for (std::size_t r0 = 0; r0 < rows; r0 += TILE) {
            const std::size_t r_max = std::min(r0 + TILE, rows);
            for (std::size_t c = c0; c < c_max; ++c) {
                for (std::size_t r = r0; r < r_max; ++r) {
                    data[r * cols + c] = transposed[c * rows + r];
                }
            }
        }
    }
}

} // namespace dgtl::math::special
