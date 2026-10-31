/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Component:   Combinatorics, Permutations, Partitions & Discrete Sequences
 * Standards:   C++26 / Clang 18+ / MSVC 19.40+ / GCC 14+
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

module;

#include <cstddef>
#include <cstdint>
#include <vector>
#include <numeric>
#include <cmath>
#include <algorithm>
#include <limits>

export module dgtl.math.combinatorics;

export namespace dgtl::math::combinatorics {

// ============================================================================
// 1. FACTORIALS & GAMMA LOGARITHMS
// ============================================================================

/**
 * @brief Exact 64-bit unsigned factorial (n <= 20).
 * Returns 0 on overflow (n > 20).
 */
[[nodiscard]] inline constexpr uint64_t factorial(uint32_t n) noexcept {
    constexpr uint64_t kFactorialTable[21] = {
        1ULL,
        1ULL,
        2ULL,
        6ULL,
        24ULL,
        120ULL,
        720ULL,
        5040ULL,
        40320ULL,
        362880ULL,
        3628800ULL,
        39916800ULL,
        479001600ULL,
        6227020800ULL,
        87178291200ULL,
        1307674368000ULL,
        20922789888000ULL,
        355687428096000ULL,
        6402373705728000ULL,
        121645100408832000ULL,
        2432902008176640000ULL
    };
    return (n <= 20) ? kFactorialTable[n] : 0ULL;
}

/**
 * @brief Natural log of factorial ln(n!).
 * Valid for arbitrarily large n using standard Stirling series / lgamma.
 */
[[nodiscard]] inline double log_factorial(uint64_t n) noexcept {
    return std::lgamma(static_cast<double>(n + 1));
}

// ============================================================================
// 2. PERMUTATIONS & COMBINATIONS (BINOMIAL COEFFICIENTS)
// ============================================================================

/**
 * @brief Permutations P(n, k) = n! / (n - k)!
 * Strict 64-bit unsigned overflow prevention.
 */
[[nodiscard]] inline constexpr uint64_t permutations(uint32_t n, uint32_t k) noexcept {
    if (k > n) return 0ULL;
    if (k == 0) return 1ULL;
    uint64_t p = 1ULL;
    for (uint32_t i = 0; i < k; ++i) {
        const uint64_t term = static_cast<uint64_t>(n - i);
        if (p > std::numeric_limits<uint64_t>::max() / term) return 0ULL; // Overflow
        p *= term;
    }
    return p;
}

/**
 * @brief Combinations (Binomial Coefficient) nCk = n! / (k! * (n - k)!)
 * Multiplicative cancellation algorithm to maximize dynamic range before 64-bit overflow.
 */
[[nodiscard]] inline constexpr uint64_t combinations(uint32_t n, uint32_t k) noexcept {
    if (k > n) return 0ULL;
    if (k == 0 || k == n) return 1ULL;
    if (k > n - k) k = n - k; // Symmetry C(n, k) = C(n, n - k)

    uint64_t result = 1ULL;
    for (uint32_t i = 1; i <= k; ++i) {
        const uint64_t num = static_cast<uint64_t>(n - k + i);
        const uint64_t den = static_cast<uint64_t>(i);

        // Divide common factor to avoid intermediate overflow
        const uint64_t g = std::gcd(result, den);
        result /= g;
        const uint64_t d = den / g;

        if (result > std::numeric_limits<uint64_t>::max() / (num / d)) {
            // High overflow fallback: return 0 or saturate
            return 0ULL;
        }
        result = (result * num) / d;
    }
    return result;
}

/**
 * @brief Floating-point combinations for large n.
 */
[[nodiscard]] inline double combinations_f64(uint64_t n, uint64_t k) noexcept {
    if (k > n) return 0.0;
    if (k == 0 || k == n) return 1.0;
    const double log_comb = std::lgamma(static_cast<double>(n + 1)) -
                            std::lgamma(static_cast<double>(k + 1)) -
                            std::lgamma(static_cast<double>(n - k + 1));
    return std::exp(log_comb);
}

/**
 * @brief Multinomial coefficient: n! / (k1! * k2! * ... * km!)
 */
[[nodiscard]] inline double multinomial(uint32_t n, const std::vector<uint32_t>& k) noexcept {
    uint32_t sum = 0;
    for (uint32_t v : k) sum += v;
    if (sum != n) return 0.0;

    double log_result = std::lgamma(static_cast<double>(n + 1));
    for (uint32_t v : k) {
        log_result -= std::lgamma(static_cast<double>(v + 1));
    }
    return std::exp(log_result);
}

// ============================================================================
// 3. STIRLING, BELL, & CATALAN NUMBERS
// ============================================================================

/**
 * @brief Stirling Numbers of the Second Kind S(n, k):
 * Number of ways to partition a set of n elements into k non-empty subsets.
 */
[[nodiscard]] inline uint64_t stirling2(uint32_t n, uint32_t k) noexcept {
    if (n == 0 && k == 0) return 1ULL;
    if (n == 0 || k == 0 || k > n) return 0ULL;
    if (k == 1 || k == n) return 1ULL;

    std::vector<uint64_t> dp(k + 1, 0ULL);
    dp[1] = 1ULL;

    for (uint32_t i = 2; i <= n; ++i) {
        const uint32_t max_j = std::min(i, k);
        for (uint32_t j = max_j; j >= 1; --j) {
            dp[j] = static_cast<uint64_t>(j) * dp[j] + dp[j - 1];
        }
    }
    return dp[k];
}

/**
 * @brief Stirling Numbers of the First Kind (Unsigned) |s(n, k)|:
 * Number of permutations of n elements with k disjoint permutation cycles.
 */
[[nodiscard]] inline uint64_t stirling1(uint32_t n, uint32_t k) noexcept {
    if (n == 0 && k == 0) return 1ULL;
    if (n == 0 || k == 0 || k > n) return 0ULL;
    if (k == n) return 1ULL;

    std::vector<uint64_t> dp(k + 1, 0ULL);
    dp[1] = 1ULL;

    for (uint32_t i = 2; i <= n; ++i) {
        const uint32_t max_j = std::min(i, k);
        for (uint32_t j = max_j; j >= 1; --j) {
            dp[j] = static_cast<uint64_t>(i - 1) * dp[j] + dp[j - 1];
        }
    }
    return dp[k];
}

/**
 * @brief Bell Number B(n):
 * Total number of partitions of a set of n elements: B(n) = sum_{k=0}^n S(n, k)
 */
[[nodiscard]] inline uint64_t bell_number(uint32_t n) noexcept {
    if (n <= 1) return 1ULL;
    std::vector<std::vector<uint64_t>> triangle(n, std::vector<uint64_t>(n, 0ULL));
    triangle[0][0] = 1ULL;

    for (uint32_t i = 1; i < n; ++i) {
        triangle[i][0] = triangle[i - 1][i - 1];
        for (uint32_t j = 1; j <= i; ++j) {
            triangle[i][j] = triangle[i][j - 1] + triangle[i - 1][j - 1];
        }
    }
    return triangle[n - 1][n - 1];
}

/**
 * @brief Catalan Number C(n) = (1 / (n + 1)) * (2n C n)
 * Models Dyck paths, binary tree structures, valid bracket expressions.
 */
[[nodiscard]] inline uint64_t catalan_number(uint32_t n) noexcept {
    if (n == 0 || n == 1) return 1ULL;
    const uint64_t c = combinations(2 * n, n);
    return (c == 0) ? 0ULL : c / static_cast<uint64_t>(n + 1);
}

/**
 * @brief Integer Partition Function p(n):
 * Number of distinct ways of representing n as a sum of positive integers.
 * Evaluates via Euler's pentagonal number theorem: O(n * sqrt(n)).
 */
[[nodiscard]] inline uint64_t integer_partitions(uint32_t n) {
    if (n == 0) return 1ULL;
    std::vector<uint64_t> p(n + 1, 0ULL);
    p[0] = 1ULL;

    for (uint32_t i = 1; i <= n; ++i) {
        int64_t sign = 1;
        for (int32_t k = 1; ; ++k) {
            const int64_t g1 = (k * (3 * k - 1)) / 2;
            const int64_t g2 = (k * (3 * k + 1)) / 2;

            if (g1 > i) break;
            if (sign > 0) p[i] += p[static_cast<std::size_t>(i - g1)];
            else p[i] -= p[static_cast<std::size_t>(i - g1)];

            if (g2 > i) break;
            if (sign > 0) p[i] += p[static_cast<std::size_t>(i - g2)];
            else p[i] -= p[static_cast<std::size_t>(i - g2)];

            sign = -sign;
        }
    }
    return p[n];
}

} // namespace dgtl::math::combinatorics
