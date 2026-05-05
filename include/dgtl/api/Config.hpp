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

#include <cstdint>
#include <string_view>

namespace dgtl {
namespace api {

/**
 * @enum ExecutionMode
 * @brief Primary execution strategy for compilation pipeline.
 */
enum class ExecutionMode : uint8_t {
    JIT = 0,        ///< Just-In-Time: Low-latency on-the-fly compilation (HFT)
    AOT = 1,        ///< Ahead-Of-Time: Offline compilation (Cloud/HPC)
    Hybrid = 2      ///< Adaptive: Switches between JIT and AOT dynamically
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
 * Encapsulates execution mode, optimization level, and feature toggles.
 */
struct Config {
    // Execution and optimization
    ExecutionMode mode = ExecutionMode::JIT;
    OptimizationLevel opt_level = OptimizationLevel::O2;
    
    // JIT-specific parameters (microseconds)
    uint64_t jit_timeout_us = 1000000;      ///< 1 second default timeout for JIT compilation
    uint32_t jit_cache_size_mb = 16;        ///< Code cache size in megabytes (16 MB default)
    
    // AOT-specific parameters
    bool aot_enable_lto = true;             ///< Link-time optimization enabled
    uint32_t aot_pass_iterations = 3;       ///< Number of optimization pass iterations
    
    // Hybrid-specific parameters
    uint32_t hybrid_tier_switch_threshold = 1000;  ///< Stream size threshold for JIT→AOT switch
    bool hybrid_adaptive_profile = true;           ///< Enable profile-guided adaptation
    
    // Feature flags
    bool enable_simd = true;                ///< SIMD vectorization enabled
    bool enable_polyhedral = false;         ///< Polyhedral loop optimization (advanced)
    bool enable_speculative = true;         ///< Speculative optimizations (guards/deopt)
    bool enable_inlining = true;            ///< Function inlining enabled
    bool deterministic_compile = false;     ///< Force deterministic compilation (disables heuristics)
    
    // Static factory for default config
    [[nodiscard]] static constexpr Config default_config() noexcept {
        return Config();
    }
    
    bool enable_temporal_fusion = true;      ///< Enable temporal loop fusion
    bool enable_polyhedral_scop = false;     ///< Enable polyhedral loop transforms
    bool enable_speculation = true;          ///< Allow speculative inlining
    bool enable_vectorization = true;        ///< Enable SIMD vectorization
    
    uint32_t thread_count = 0;               ///< 0 = use hardware concurrency
    uint32_t jit_timeout_us = 1000000;       ///< 1 second default JIT timeout
    
    /**
     * @brief Validate configuration consistency.
     * @return true if all settings are valid
     */
    [[nodiscard]] bool is_valid() const noexcept {
        if (thread_count > 256) return false;  // Sanity check
        if (jit_timeout_us == 0) return false; // Must have non-zero timeout
        return true;
    }
    
    /**
     * @brief Get human-readable execution mode name.
     */
    [[nodiscard]] std::string_view get_mode_name() const noexcept {
        switch (mode) {
            case ExecutionMode::JIT:    return "JIT";
            case ExecutionMode::AOT:    return "AOT";
            case ExecutionMode::Hybrid: return "Hybrid";
            default:                    return "Unknown";
        }
    }
    
    /**
     * @brief Get human-readable optimization level name.
     */
    [[nodiscard]] std::string_view get_opt_level_name() const noexcept {
        switch (opt_level) {
            case OptimizationLevel::O0:      return "O0";
            case OptimizationLevel::O1:      return "O1";
            case OptimizationLevel::O2:      return "O2";
            case OptimizationLevel::O3:      return "O3";
            case OptimizationLevel::OStrike: return "OStrike";
            default:                         return "Unknown";
        }
    }
};

/**
 * @brief Default configuration for HFT workloads (low latency critical).
 */
[[nodiscard]] inline Config hft_config() noexcept {
    return Config{
        .mode = ExecutionMode::JIT,
        .opt_level = OptimizationLevel::OStrike,
        .enable_temporal_fusion = true,
        .enable_polyhedral_scop = false,
        .enable_speculation = true,
        .enable_vectorization = true,
        .thread_count = 1,
        .jit_timeout_us = 500000  // 500µs for HFT
    };
}

/**
 * @brief Default configuration for Cloud/HPC workloads (throughput).
 */
[[nodiscard]] inline Config cloud_config() noexcept {
    return Config{
        .mode = ExecutionMode::AOT,
        .opt_level = OptimizationLevel::O3,
        .enable_temporal_fusion = true,
        .enable_polyhedral_scop = true,
        .enable_speculation = true,
        .enable_vectorization = true,
        .thread_count = 0,         // Use all cores
        .jit_timeout_us = 10000000 // 10 seconds for AOT
    };
}

/**
 * @brief Default configuration for balanced use (development).
 */
[[nodiscard]] inline Config default_config() noexcept {
    return Config{
        .mode = ExecutionMode::Hybrid,
        .opt_level = OptimizationLevel::O2,
        .enable_temporal_fusion = true,
        .enable_polyhedral_scop = false,
        .enable_speculation = true,
        .enable_vectorization = true,
        .thread_count = 0,
        .jit_timeout_us = 1000000
    };
}

} // namespace api
} // namespace dgtl
