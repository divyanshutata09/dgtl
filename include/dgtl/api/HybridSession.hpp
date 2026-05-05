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
#include "dgtl/pipeline/modes/Hybrid_Pipeline.hpp"
#include <memory>

namespace dgtl {
namespace api {

class Context;

/**
 * @class HybridSession
 * @brief Adaptive tiered execution engine.
 * 
 * Dynamically selects between JIT and AOT based on runtime characteristics
 * of code being compiled. JIT handles cold paths with minimal latency,
 * while AOT handles hot loops with maximum optimization intensity.
 * Intelligent tier switching optimizes for the best of both worlds.
 */
class HybridSession : public ExecutionEngine {
public:
    /**
     * @brief Create a hybrid session for the given context.
     * @param context The execution context
     */
    explicit HybridSession(Context& context) noexcept;

    ~HybridSession() override = default;

    /**
     * @brief Compile IRStream with adaptive tier selection.
     * @param stream The IR to compile
     * @return true if compilation succeeded
     */
    [[nodiscard]] bool compile(ir::IRStream& stream) noexcept override;

    /**
     * @brief Resolve a symbol by name from hybrid compiled code.
     * @param symbol_name The name of the symbol
     * @return Address of compiled code (0 if not found)
     */
    [[nodiscard]] uint64_t get_symbol_address(const char* symbol_name) noexcept override;

    /**
     * @brief Toggle execution tier preference.
     * @param prefer_jit If true, prefer JIT for next compilations. If false, prefer AOT.
     */
    void toggle_mode(bool prefer_jit) noexcept;

    /**
     * @brief Get current execution tier.
     * @return The active tier (JIT_Fast, AOT_Intensive, or Adaptive)
     */
    [[nodiscard]] pipeline::ExecutionTier get_current_tier() const noexcept;

    /**
     * @brief Get internal hybrid pipeline for advanced tuning.
     */
    [[nodiscard]] pipeline::Hybrid_Pipeline& get_pipeline() noexcept { return pipeline_; }

private:
    Context& context_;
    pipeline::Hybrid_Pipeline pipeline_;

    HybridSession(const HybridSession&) = delete;
    HybridSession& operator=(const HybridSession&) = delete;
};

} // namespace api
} // namespace dgtl
