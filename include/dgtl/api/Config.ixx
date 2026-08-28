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

#include <cstdint>
#include <string_view>

export module dgtl.config;

export namespace dgtl {
namespace api {

/**
 * @enum ExecutionMode
 * @brief Primary execution strategy for compilation pipeline.
 */
enum class ExecutionMode : uint8_t {
    JIT = 0,        ///< Just-In-Time: Low-latency on-the-fly compilation (HFT)
    AOT = 1,        ///< Ahead-Of-Time: Offline compilation (Cloud/HPC)
    Adaptive = 2    ///< Adaptive: Switches between JIT and AOT dynamically
};

/**
 * @enum OptimizationLevel
 * @brief Optimization intensity and target use case.
 */
enum class OptimizationLevel : uint8_t {
    O0 = 0,         ///< Minimal: Debug mode, preserve semantics
    O1 = 1,         ///< Conservative: Basic local opts only
    O2 = 2,         ///< Default: Balanced optimization intensity
    O3 = 3,         ///< Aggressive: Advanced speculative transforms
    OStrike = 4     ///< HFT-Specialized: Binary-size critical, microsecond-latency targets
};

/**
 * @struct Config
 * @brief Runtime configuration for DGTL compilation pipeline.
 * Encapsulates execution parameters.
 */
struct Config {
    ExecutionMode mode = ExecutionMode::JIT;
    OptimizationLevel opt_level = OptimizationLevel::O3;
    uint64_t _timeout_us = 1000000;
    bool enable_temporal_fusion = true;
    bool enable_polyhedral_scop = true;
    bool enable_speculation = true;
    bool enable_vectorization = true;
    uint32_t thread_count = 0;
};

/**
 * @brief Default configuration for HFT workloads (low latency critical).
 * [STAFF NOTE]: Upgraded to constexpr for zero-cost static initialization.
 */
[[nodiscard]] inline constexpr Config hft_config() noexcept {
    return Config{
        .mode = ExecutionMode::JIT,
        .opt_level = OptimizationLevel::OStrike,
        ._timeout_us = 500000,  // 500us for HFT
        .enable_temporal_fusion = true,
        .enable_polyhedral_scop = false,
        .enable_speculation = true,
        .enable_vectorization = true,
        .thread_count = 1
    };
}

/**
 * @brief Default configuration for Cloud/HPC workloads (throughput).
 * [STAFF NOTE]: Upgraded to constexpr.
 */
[[nodiscard]] inline constexpr Config cloud_config() noexcept {
    return Config{
        .mode = ExecutionMode::AOT,
        .opt_level = OptimizationLevel::O3,
        ._timeout_us = 10000000, // 10 seconds for 
        .enable_temporal_fusion = true,
        .enable_polyhedral_scop = true,
        .enable_speculation = true,
        .enable_vectorization = true,
        .thread_count = 0          // Use all cores
    };
}

/**
 * @brief Default configuration for balanced use (development).
 * [STAFF NOTE]: Upgraded to constexpr.
 */
[[nodiscard]] inline constexpr Config default_config() noexcept {
    return Config{
        .mode = ExecutionMode::Adaptive,
        .opt_level = OptimizationLevel::O3,
        ._timeout_us = 1000000, // 1 second
        .enable_temporal_fusion = true,
        .enable_polyhedral_scop = true,
        .enable_speculation = true,
        .enable_vectorization = true,
        .thread_count = 0
    };
}

} // namespace api
} // namespace dgtl
