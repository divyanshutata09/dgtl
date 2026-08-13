/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:    DGTL (Deep General-purpose Transformation Layer)
 * Owner:      Divyanshu Tata
 * Repository: github.com/TataG-Systems/DGTL
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

module;

#include <cstdlib>

// Global Module Fragment: Include lightweight traditional headers here
#include <cstdio>
#include <cstdlib>
#include <cassert>
#include <cstdint>
#include <type_traits>

export module dgtl.assertions;

// ====================================================================
// INTERNAL INFRASTRUCTURE (Implementation Details)
// ====================================================================

namespace dgtl::utils::detail {

[[noreturn]] inline void assert_fail(const char* condition,
                                     const char* message,
                                     const char* file,
                                     int line) noexcept {
    if (message != nullptr) {
        std::fprintf(stderr,
                     "DGTL Assertion failed: %s\nMessage: %s\nFile: %s:%d\n",
                     condition,
                     message,
                     file,
                     line);
    } else {
        std::fprintf(stderr,
                     "DGTL Assertion failed: %s at %s:%d\n",
                     condition,
                     file,
                     line);
    }
    std::fflush(stderr);
    std::abort();
}

[[noreturn]] inline void unreachable_fail(const char* file, int line) noexcept {
    std::fprintf(stderr, "DGTL Unreachable code reached at %s:%d\n", file, line);
    std::fflush(stderr);
    std::abort();
}

template <typename Index, typename Size>
[[nodiscard]] constexpr bool is_index_in_bounds(Index index, Size size) noexcept {
    if constexpr (std::is_signed_v<Size>) {
        if (size <= 0) {
            return false;
        }
    }

    using IndexDecayed = std::remove_cv_t<std::remove_reference_t<Index>>;
    using SizeDecayed = std::remove_cv_t<std::remove_reference_t<Size>>;

    if constexpr (std::is_signed_v<IndexDecayed>) {
        if (index < 0) {
            return false;
        }
    }

    using CommonUnsigned = std::common_type_t<std::make_unsigned_t<IndexDecayed>,
                                              std::make_unsigned_t<SizeDecayed>>;
    const auto idx_u = static_cast<CommonUnsigned>(index);
    const auto size_u = static_cast<CommonUnsigned>(size);
    return idx_u < size_u;
}

} // namespace dgtl::utils::detail


// ====================================================================
// EXPORTED API FOR MYZON & INTERNAL ENGINE
// ====================================================================

export namespace dgtl::utils {

/**
 * High-performance trap for fatal compiler errors.
 * Lightweight std::fprintf to drastically reduce compile times.
 */
[[noreturn]] inline void fatal_compile_error(const char* context, std::uint32_t node_id) {
    std::fprintf(stderr, "[DGTL FATAL] Compilation halted in: %s at Node Virtual ID: %u\nReason: Unrecoverable IR to Machine Code lowering failure.\n", 
                 context, node_id);
    std::fflush(stderr);
    std::abort();
}

} // namespace dgtl::utils


// ====================================================================
// ASSERTION MACROS - MACRO DEFINITIONS WITHIN MODULE SCOPE
// ====================================================================

/**
 * @def DGTL_ASSERT
 * Always active. Uses C++20 [[unlikely]] to prevent instruction cache pollution.
 */
#define DGTL_ASSERT(condition) \
    do { \
        if (!(condition)) [[unlikely]] { \
            ::dgtl::utils::detail::assert_fail(#condition, nullptr, __FILE__, __LINE__); \
        } \
    } while (0)

/**
 * @def DGTL_ASSERT_MSG
 * Assertion with custom error message.
 */
#define DGTL_ASSERT_MSG(condition, message) \
    do { \
        if (!(condition)) [[unlikely]] { \
            ::dgtl::utils::detail::assert_fail(#condition, message, __FILE__, __LINE__); \
        } \
    } while (0)

/**
 * @def DGTL_ASSERT_BOUNDS
 * Casts to size_t to safely handle both signed/unsigned mismatches 
 * without triggering -Wtype-limits warnings.
 */
#define DGTL_ASSERT_BOUNDS(index, size) \
    do { \
        if (!::dgtl::utils::detail::is_index_in_bounds((index), (size))) [[unlikely]] { \
            std::fprintf(stderr, "DGTL Bounds check failed: index=%zu, size=%zu at %s:%d\n", \
                         static_cast<std::size_t>(index), static_cast<std::size_t>(size), __FILE__, __LINE__); \
            std::fflush(stderr); \
            std::abort(); \
        } \
    } while (0)

/**
 * @def DGTL_ASSERT_NOT_NULL
 * Null pointer assertion.
 */
#define DGTL_ASSERT_NOT_NULL(ptr) \
    do { \
        if (!(ptr)) [[unlikely]] { \
            ::dgtl::utils::detail::assert_fail(#ptr " != nullptr", "Null pointer check failed", __FILE__, __LINE__); \
        } \
    } while (0)

/**
 * @def DGTL_UNREACHABLE
 * Mark unreachable code path. Uses standard compliance layout.
 */
#define DGTL_UNREACHABLE() \
    do { \
        ::dgtl::utils::detail::unreachable_fail(__FILE__, __LINE__); \
    } while (0)

/**
 * @def DGTL_ASSUME
 * Native optimization attributes across toolchains.
 */
#if defined(_MSC_VER)
    #define DGTL_ASSUME(condition) __assume(condition)
#elif defined(__clang__)
    #define DGTL_ASSUME(condition) __builtin_assume(condition)
#elif defined(__GNUC__) && (__GNUC__ >= 13)
    #define DGTL_ASSUME(condition) __builtin_assume(condition)
#else
    #define DGTL_ASSUME(condition) \
        do { \
            if (!(condition)) [[unlikely]] { \
                ::dgtl::utils::detail::unreachable_fail(__FILE__, __LINE__); \
            } \
        } while (0)
#endif

/**
 * @def DGTL_CHECK
 * Alias for DGTL_ASSERT for consistency.
 */
#define DGTL_CHECK(condition) DGTL_ASSERT(condition)