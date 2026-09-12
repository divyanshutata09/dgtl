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
#include <memory>
#include <string>
#include <string_view>

export module dgtl.internal.api_execution_engine;

import dgtl.ir_core;
import dgtl.numa_runtime;
import dgtl.compiler_context;

export namespace dgtl {

// FIXED: Removed duplicate forward declarations of VerificationResult, 
// IRStream, and ExecutionFeedback since they are fully imported via dgtl.ir_core!

namespace api {

namespace numa = dgtl::runtime::numa;

/**
 * @class ExecutionEngine
 * @brief Abstract base class for compilation and execution engines.
 * * Defines the virtual interface for // execution strategies.
 * Subclasses implement specific compilation and symbol resolution policies.
 * * [STAFF NOTE]: This defines the primary polymorphic ABI boundary for the backend.
 * Instances are thread-compatible but not thread-safe; external sync required.
 */
class ExecutionEngine {
public:
    virtual ~ExecutionEngine() = default;

    [[nodiscard]] virtual formal::VerificationResult verify_semantics(
        const ir::IRStream& original_stream,
        const ir::IRStream& optimized_stream,
        std::string_view pass_name,
        std::string_view solver_name = "z3"
    ) noexcept = 0;

    [[nodiscard]] virtual bool specialize_stream(
        ir::IRStream& stream,
        const ir::evolution::ExecutionFeedback& feedback
    ) noexcept = 0;

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
     * @brief Convenience overload for std::string symbol names.
     * @param symbol_name The name of the symbol to resolve
     * @return Address of the symbol (0 if not found)
     */
    [[nodiscard]] uint64_t get_symbol_address(const std::string& symbol_name) noexcept {
        return get_symbol_address(symbol_name.c_str());
    }

    /**
     * @brief Resolve a native function pointer by name for zero-overhead invocation.
     * @param symbol_name The name of the symbol to resolve
     * @return Raw function pointer (nullptr if not found)
     */
    [[nodiscard]] void* get_function_pointer(const char* symbol_name) noexcept {
        return reinterpret_cast<void*>(get_symbol_address(symbol_name));
    }

    [[nodiscard]] void* get_function_pointer(const std::string& symbol_name) noexcept {
        return get_function_pointer(symbol_name.c_str());
    }

    /**
     * @brief Register a runtime function address by name.
     */
    virtual void register_host_symbol(const std::string& symbol_name, uint64_t address) noexcept = 0;

    /**
     * @brief Register a runtime function address for CALL nodes by callee slot.
     */
    virtual void register_host_symbol_slot(uint32_t slot, uint64_t address) noexcept = 0;

    /**
     * @brief Factory method to create appropriate engine for the context.
     * @param context The execution context
     * @return A new ExecutionEngine instance (// based on config)
     */
    [[nodiscard]] static std::unique_ptr<ExecutionEngine> create(Context& context) noexcept;

protected:
    ExecutionEngine() = default;
    ExecutionEngine(const ExecutionEngine&) = delete;
    ExecutionEngine& operator=(const ExecutionEngine&) = delete;
};

} // namespace api
} // namespace dgtl