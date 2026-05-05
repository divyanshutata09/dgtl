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
#include "dgtl/pipeline/modes/AOT_Pipeline.hpp"
#include <memory>
#include <string_view>

namespace dgtl {
namespace api {

class Context;

/**
 * @class AOTSession
 * @brief Ahead-Of-Time (AOT) execution engine.
 * 
 * Compiles entire programs offline with maximum optimization intensity.
 * Suitable for Cloud/HPC workloads where compilation latency is amortized
 * over long-running computations. Supports static binary and object emission.
 */
class AOTSession : public ExecutionEngine {
public:
    /**
     * @brief Create an AOT session for the given context.
     * @param context The execution context
     */
    explicit AOTSession(Context& context) noexcept;

    ~AOTSession() override = default;

    /**
     * @brief Compile IRStream with maximum AOT optimizations.
     * @param stream The IR to compile
     * @return true if compilation succeeded
     */
    [[nodiscard]] bool compile(ir::IRStream& stream) noexcept override;

    /**
     * @brief Resolve a symbol by name from AOT compiled code.
     * @param symbol_name The name of the symbol
     * @return Address of compiled code (0 if not found)
     */
    [[nodiscard]] uint64_t get_symbol_address(const char* symbol_name) noexcept override;

    /**
     * @brief Emit compiled code as relocatable object file.
     * @param output_path Path to write .o file
     * @return true if emission succeeded
     */
    [[nodiscard]] bool emit_object(std::string_view output_path) noexcept;

    /**
     * @brief Emit compiled code as standalone executable binary.
     * @param output_path Path to write binary executable
     * @return true if emission succeeded
     */
    [[nodiscard]] bool emit_binary(std::string_view output_path) noexcept;

    /**
     * @brief Get internal AOT pipeline for advanced tuning.
     */
    [[nodiscard]] pipeline::AOT_Pipeline& get_pipeline() noexcept { return pipeline_; }

private:
    Context& context_;
    pipeline::AOT_Pipeline pipeline_;

    AOTSession(const AOTSession&) = delete;
    AOTSession& operator=(const AOTSession&) = delete;
};

} // namespace api
} // namespace dgtl
