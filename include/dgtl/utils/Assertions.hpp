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

#pragma once

#include <cstdio>
#include <cstdlib>
#include <cassert>

namespace dgtl {
namespace utils {

// ====================================================================
// ASSERTION MACROS - PRODUCTION-GRADE ERROR HANDLING
// ====================================================================

/**
 * @def DGTL_ASSERT
 * @brief Assertion that triggers std::abort() on failure.
 * Always active (debug and release modes).
 */
#define DGTL_ASSERT(condition) \
    do { \
        if (!(condition)) { \
            std::fprintf(stderr, "DGTL Assertion failed: %s at %s:%d\n", \
                         #condition, __FILE__, __LINE__); \
            std::fflush(stderr); \
            std::abort(); \
        } \
    } while (0)

/**
 * @def DGTL_ASSERT_MSG
 * @brief Assertion with custom error message.
 */
#define DGTL_ASSERT_MSG(condition, message) \
    do { \
        if (!(condition)) { \
            std::fprintf(stderr, "DGTL Assertion failed: %s\nMessage: %s\nFile: %s:%d\n", \
                         #condition, message, __FILE__, __LINE__); \
            std::fflush(stderr); \
            std::abort(); \
        } \
    } while (0)

/**
 * @def DGTL_ASSERT_BOUNDS
 * @brief Bounds checking assertion.
 */
#define DGTL_ASSERT_BOUNDS(index, size) \
    do { \
        if ((index) >= (size) || (index) < 0) { \
            std::fprintf(stderr, "DGTL Bounds check failed: index=%ld, size=%ld at %s:%d\n", \
                         static_cast<long>(index), static_cast<long>(size), __FILE__, __LINE__); \
            std::fflush(stderr); \
            std::abort(); \
        } \
    } while (0)

/**
 * @def DGTL_ASSERT_NOT_NULL
 * @brief Null pointer assertion.
 */
#define DGTL_ASSERT_NOT_NULL(ptr) \
    do { \
        if (!(ptr)) { \
            std::fprintf(stderr, "DGTL Null pointer check failed: %s at %s:%d\n", \
                         #ptr, __FILE__, __LINE__); \
            std::fflush(stderr); \
            std::abort(); \
        } \
    } while (0)

/**
 * @def DGTL_UNREACHABLE
 * @brief Mark unreachable code path.
 */
#define DGTL_UNREACHABLE() \
    do { \
        std::fprintf(stderr, "DGTL Unreachable code reached at %s:%d\n", __FILE__, __LINE__); \
        std::fflush(stderr); \
        __builtin_unreachable(); \
    } while (0)

/**
 * @def DGTL_ASSUME
 * @brief Compiler hint for optimization.
 * Used for code paths we know are unreachable but compiler can't prove.
 */
#define DGTL_ASSUME(condition) \
    do { \
        if (!(condition)) { \
            __builtin_unreachable(); \
        } \
    } while (0)

/**
 * @def DGTL_CHECK
 * @brief Alias for DGTL_ASSERT for consistency.
 */
#define DGTL_CHECK(condition) DGTL_ASSERT(condition)

} // namespace utils
} // namespace dgtl
