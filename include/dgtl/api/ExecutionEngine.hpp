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

#include "dgtl/ir/Global_IR.hpp"
#include <cstdint>
#include <memory>

namespace dgtl {
namespace api {

class Context;

/**
 * @class ExecutionEngine
 * @brief Abstract base class for compilation and execution engines.
 * * Defines the virtual interface for JIT/AOT/Hybrid execution strategies.
 * Subclasses implement specific compilation and symbol resolution policies.
 * * [STAFF NOTE]: This defines the primary polymorphic ABI boundary for the backend.
 * Instances are thread-compatible but not thread-safe; external sync required.
 */
class ExecutionEngine {
public:
    virtual ~ExecutionEngine() = default;

    /**
     * @brief Compile an IRStream to executable code.
     * @param stream The IR to compile
     * @return true if compilation succeeded
     */
    [[nodiscard]] virtual bool compile(ir::IRStream& stream) noexcept = 0;

    /**
     * @brief Resolve a symbol address by name.
     * @param symbol_name The name of the symbol to resolve
     * @return Address of the symbol (0 if not found)
     */
    [[nodiscard]] virtual uint64_t get_symbol_address(const char* symbol_name) noexcept = 0;

    /**
     * @brief Factory method to create appropriate engine for the context.
     * @param context The execution context
     * @return A new ExecutionEngine instance (JIT/AOT/Hybrid based on config)
     */
    [[nodiscard]] static std::unique_ptr<ExecutionEngine> create(Context& context) noexcept;

protected:
    ExecutionEngine() = default;
    ExecutionEngine(const ExecutionEngine&) = delete;
    ExecutionEngine& operator=(const ExecutionEngine&) = delete;
};

} // namespace api
} // namespace dgtl