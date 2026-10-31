/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Component:   Probability, Statistical Distributions, Moments & PRNG
 * Standards:   C++26 / Clang 18+ / MSVC 19.40+ / GCC 14+
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

module;

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>
#include <algorithm>
#include <numeric>
#include <limits>
#include <numbers>

export module dgtl.math.probability;

export namespace dgtl::math::probability {

// ============================================================================
// 1. STATISTICAL MOMENTS & DESCRIPTIVE METRICS
// ============================================================================

[[nodiscard]] inline double mean(const std::vector<double>& data) noexcept {
    if (data.empty()) return 0.0;
    const double sum = std::accumulate(data.begin(), data.end(), 0.0);
    return sum / static_cast<double>(data.size());
}

[[nodiscard]] inline double variance(const std::vector<double>& data, bool sample = true) noexcept {
    if (data.size() < 2) return 0.0;
    const double m = mean(data);
    double sum_sq = 0.0;
    for (double x : data) {
        const double d = x - m;
        sum_sq += d * d;
    }
    const double denom = static_cast<double>(sample ? data.size() - 1 : data.size());
    return sum_sq / denom;
}

[[nodiscard]] inline double std_dev(const std::vector<double>& data, bool sample = true) noexcept {
    return std::sqrt(variance(data, sample));
}

/**
 * @brief Skewness (3rd standardized moment).
 * Measures asymmetry of probability distribution.
 */
[[nodiscard]] inline double skewness(const std::vector<double>& data) noexcept {
    const std::size_t n = data.size();
    if (n < 3) return 0.0;
    const double m = mean(data);
    const double s = std_dev(data, true);
    if (s <= 1e-15) return 0.0;

    double m3 = 0.0;
    for (double x : data) {
        const double d = (x - m) / s;
        m3 += d * d * d;
    }
    const double factor = static_cast<double>(n) / static_cast<double>((n - 1) * (n - 2));
    return factor * m3;
}

/**
 * @brief Kurtosis (4th standardized moment / excess kurtosis relative to Gaussian).
 */
[[nodiscard]] inline double excess_kurtosis(const std::vector<double>& data) noexcept {
    const std::size_t n = data.size();
    if (n < 4) return 0.0;
    const double m = mean(data);
    const double s = std_dev(data, true);
    if (s <= 1e-15) return 0.0;

    double m4 = 0.0;
    for (double x : data) {
        const double d = (x - m) / s;
        m4 += d * d * d * d;
    }

    const double n_d = static_cast<double>(n);
    const double c1 = (n_d * (n_d + 1.0)) / ((n_d - 1.0) * (n_d - 2.0) * (n_d - 3.0));
    const double c2 = (3.0 * (n_d - 1.0) * (n_d - 1.0)) / ((n_d - 2.0) * (n_d - 3.0));
    return c1 * m4 - c2;
}

[[nodiscard]] inline double covariance(const std::vector<double>& x, const std::vector<double>& y) noexcept {
    if (x.size() != y.size() || x.size() < 2) return 0.0;
    const double mx = mean(x);
    const double my = mean(y);
    double cov = 0.0;
    for (std::size_t i = 0; i < x.size(); ++i) {
        cov += (x[i] - mx) * (y[i] - my);
    }
    return cov / static_cast<double>(x.size() - 1);
}

[[nodiscard]] inline double pearson_correlation(const std::vector<double>& x, const std::vector<double>& y) noexcept {
    const double sx = std_dev(x, true);
    const double sy = std_dev(y, true);
    if (sx <= 1e-15 || sy <= 1e-15) return 0.0;
    return covariance(x, y) / (sx * sy);
}

// ============================================================================
// 2. PROBABILITY DISTRIBUTIONS (PDF, CDF, QUANTILE)
// ============================================================================

namespace detail {
    inline constexpr double kInvSqrt2Pi = 0.398942280401432677939946;
    inline constexpr double kSqrt2      = 1.4142135623730950488016887;
}

/**
 * @brief Standard normal PDF phi(z)
 */
[[nodiscard]] inline double normal_pdf(double x, double mu = 0.0, double sigma = 1.0) noexcept {
    if (sigma <= 0.0) return 0.0;
    const double z = (x - mu) / sigma;
    return (detail::kInvSqrt2Pi / sigma) * std::exp(-0.5 * z * z);
}

/**
 * @brief Standard normal CDF Phi(z) via erfc
 */
[[nodiscard]] inline double normal_cdf(double x, double mu = 0.0, double sigma = 1.0) noexcept {
    if (sigma <= 0.0) return x < mu ? 0.0 : 1.0;
    const double z = (x - mu) / (sigma * detail::kSqrt2);
    return 0.5 * std::erfc(-z);
}

/**
 * @brief Inverse Normal CDF (Quantile / Probit Function) using rational approximation.
 * Accuracy: ~1e-9 across full probability domain (0, 1).
 */
[[nodiscard]] inline double normal_quantile(double p) noexcept {
    if (p <= 0.0) return -std::numeric_limits<double>::infinity();
    if (p >= 1.0) return  std::numeric_limits<double>::infinity();

    // Coefficients in rational approximations
    constexpr double a1 = -3.969683028665376e+01;
    constexpr double a2 =  2.209460984245205e+02;
    constexpr double a3 = -2.759285104469687e+02;
    constexpr double a4 =  1.383577518672690e+02;
    constexpr double a5 = -3.066479806614716e+01;
    constexpr double a6 =  2.506628277459239e+00;

    constexpr double b1 = -5.447609879822406e+01;
    constexpr double b2 =  1.615858368580409e+02;
    constexpr double b3 = -1.556989798598866e+02;
    constexpr double b4 =  6.680131188771972e+01;
    constexpr double b5 = -1.328068155288572e+01;

    constexpr double c1 = -7.784894002430293e-03;
    constexpr double c2 = -3.223964580411365e-01;
    constexpr double c3 = -2.400758277161838e+00;
    constexpr double c4 = -2.549732539343734e+00;
    constexpr double c5 =  4.374664141464968e+00;
    constexpr double c6 =  2.938163982698783e+00;

    constexpr double d1 =  7.784695709041462e-03;
    constexpr double d2 =  3.224671290700398e-01;
    constexpr double d3 =  2.445134137142996e+00;
    constexpr double d4 =  3.754408661907416e+00;

    constexpr double p_low = 0.02425;
    constexpr double p_high = 1.0 - p_low;

    if (p < p_low) {
        // Rational approximation for lower region
        const double q = std::sqrt(-2.0 * std::log(p));
        return (((((c1 * q + c2) * q + c3) * q + c4) * q + c5) * q + c6) /
               ((((d1 * q + d2) * q + d3) * q + d4) * q + 1.0);
    }
    if (p <= p_high) {
        // Rational approximation for central region
        const double q = p - 0.5;
        const double r = q * q;
        return (((((a1 * r + a2) * r + a3) * r + a4) * r + a5) * r + a6) * q /
               (((((b1 * r + b2) * r + b3) * r + b4) * r + b5) * r + 1.0);
    }
    // Rational approximation for upper region
    const double q = std::sqrt(-2.0 * std::log(1.0 - p));
    return -(((((c1 * q + c2) * q + c3) * q + c4) * q + c5) * q + c6) /
            ((((d1 * q + d2) * q + d3) * q + d4) * q + 1.0);
}

/**
 * @brief Student's t-distribution PDF with nu degrees of freedom.
 */
[[nodiscard]] inline double student_t_pdf(double t, double nu) noexcept {
    if (nu <= 0.0) return 0.0;
    const double log_coeff = std::lgamma(0.5 * (nu + 1.0)) - std::lgamma(0.5 * nu) -
                             0.5 * std::log(nu * std::numbers::pi);
    const double log_body = -0.5 * (nu + 1.0) * std::log1p((t * t) / nu);
    return std::exp(log_coeff + log_body);
}

/**
 * @brief Chi-squared distribution PDF with k degrees of freedom.
 */
[[nodiscard]] inline double chi_squared_pdf(double x, double k) noexcept {
    if (x <= 0.0 || k <= 0.0) return 0.0;
    const double log_pdf = (0.5 * k - 1.0) * std::log(x) - 0.5 * x -
                           (0.5 * k * std::log(2.0)) - std::lgamma(0.5 * k);
    return std::exp(log_pdf);
}

/**
 * @brief Poisson probability mass function: P(X = k) = lambda^k * e^(-lambda) / k!
 */
[[nodiscard]] inline double poisson_pmf(uint64_t k, double lambda) noexcept {
    if (lambda <= 0.0) return k == 0 ? 1.0 : 0.0;
    const double log_p = static_cast<double>(k) * std::log(lambda) - lambda - std::lgamma(static_cast<double>(k + 1));
    return std::exp(log_p);
}

/**
 * @brief Binomial probability mass function: P(X = k) = nCk * p^k * (1 - p)^(n - k)
 */
[[nodiscard]] inline double binomial_pmf(uint64_t k, uint64_t n, double p) noexcept {
    if (k > n || p < 0.0 || p > 1.0) return 0.0;
    if (p == 0.0) return k == 0 ? 1.0 : 0.0;
    if (p == 1.0) return k == n ? 1.0 : 0.0;
    const double log_nck = std::lgamma(static_cast<double>(n + 1)) -
                           std::lgamma(static_cast<double>(k + 1)) -
                           std::lgamma(static_cast<double>(n - k + 1));
    const double log_prob = log_nck + static_cast<double>(k) * std::log(p) +
                            static_cast<double>(n - k) * std::log(1.0 - p);
    return std::exp(log_prob);
}

// ============================================================================
// 3. STATISTICALLY RIGOROUS PSEUDO-RANDOM NUMBER GENERATORS (PRNG)
// ============================================================================

/**
 * @brief Xoshiro256** Generator. State: 4 x 64-bit uints. Period: 2^256 - 1.
 */
class Xoshiro256StarStar {
private:
    uint64_t s_[4];

    [[nodiscard]] static constexpr uint64_t rotl(uint64_t x, int k) noexcept {
        return (x << k) | (x >> (64 - k));
    }

public:
    explicit Xoshiro256StarStar(uint64_t seed = 0x853C49E6748FEA9BULL) noexcept {
        // Initialize state via SplitMix64
        for (auto& s : s_) {
            seed += 0x9E3779B97F4A7C15ULL;
            uint64_t z = seed;
            z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
            z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
            s = z ^ (z >> 31);
        }
    }

    [[nodiscard]] uint64_t next_u64() noexcept {
        const uint64_t result = rotl(s_[1] * 5, 7) * 9;
        const uint64_t t = s_[1] << 17;

        s_[2] ^= s_[0];
        s_[3] ^= s_[1];
        s_[1] ^= s_[2];
        s_[0] ^= s_[3];

        s_[2] ^= t;
        s_[3] = rotl(s_[3], 45);

        return result;
    }

    [[nodiscard]] double next_f64() noexcept {
        // Convert to double in [0, 1) using upper 53 bits
        return static_cast<double>(next_u64() >> 11) * 0x1.0p-53;
    }

    /**
     * @brief Generates Gaussian variate N(mu, sigma^2) via Box-Muller transform.
     */
    [[nodiscard]] double next_gaussian(double mu = 0.0, double sigma = 1.0) noexcept {
        double u1 = next_f64();
        while (u1 <= 1e-15) u1 = next_f64();
        const double u2 = next_f64();
        const double z = std::sqrt(-2.0 * std::log(u1)) * std::cos(2.0 * std::numbers::pi * u2);
        return mu + sigma * z;
    }
};

} // namespace dgtl::math::probability
