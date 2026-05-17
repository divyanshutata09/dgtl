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

#include "dgtl/api/ExecutionEngine.hpp"
#include "dgtl/pipeline/Pass_Manager.hpp"
#include <memory>

namespace dgtl {
namespace api {

class Context;

/**
 * @class JITSession
 * @brief Just-In-Time execution engine.
 * * Optimized for low-latency on-the-fly compilation of hot paths.
 * Suitable for HFT and latency-critical workloads where compilation
 * must complete in microseconds.
 * * [STAFF NOTE]: Marked final for devirtualization bypass. Hardware 
 * cache-line aligned to prevent CPU cache thrashing during compilation.
 */
class alignas(64) JITSession final : public ExecutionEngine {
public:
    /**
     * @brief Create a JIT session for the given context.
     * @param context The execution context
     */
    explicit JITSession(Context& context) noexcept;

    ~JITSession() override = default;

    /**
     * @brief Compile IRStream with JIT optimizations (fast, minimal).
     * @param stream The IR to compile
     * @return true if compilation succeeded within timeout
     */
    [[nodiscard]] bool compile(ir::IRStream& stream) noexcept override;

    /**
     * @brief Resolve a symbol by name from JIT code cache.
     * @param symbol_name The name of the symbol
     * @return Address of compiled code (0 if not found)
     */
    [[nodiscard]] uint64_t get_symbol_address(const char* symbol_name) noexcept override;

    /**
     * @brief Get internal JIT pipeline for advanced tuning.
     */
    [[nodiscard]] pipeline::JIT_Pipeline& get_pipeline() noexcept { return pipeline_; }

private:
    Context& context_;
    
    // 🚀 HFT UPGRADE: Situation of pipeline state on the aligned boundary 
    // to ensure no cross-core contention during concurrent JIT strikes.
    pipeline::JIT_Pipeline pipeline_;

    JITSession(const JITSession&) = delete;
    JITSession& operator=(const JITSession&) = delete;
};

} // namespace api
} // namespace dgtl