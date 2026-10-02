/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
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
#include <type_traits>

export module dgtl.api.execution_engine;

import dgtl.ir;
import dgtl.runtime.numa;
import dgtl.compiler_context;
import dgtl.mir;
import dgtl.object.file;
export import dgtl.formal;

export namespace dgtl {
namespace api {

namespace numa = dgtl::runtime::numa;

/**
 * @class ExecutionEngine
 * @brief Sovereign polymorphic base class for compilation and execution engines.
 * Supports whole-program IRStream, MIR MachineFunction, and linkable ObjectFile execution.
 */
class ExecutionEngine {
public:
    virtual ~ExecutionEngine() = default;

    [[nodiscard]] virtual formal::VerificationResult verify_semantics(
        const ir::IRStream& original_stream,
        const ir::IRStream& optimized_stream,
        std::string_view pass_name,
        std::string_view solver_name = "z3"
    ) = 0;

    [[nodiscard]] virtual bool specialize_stream(
        ir::IRStream& stream,
        const ir::evolution::ExecutionFeedback& feedback
    ) = 0;

    /**
     * @brief Compile an IRStream to executable code.
     */
    [[nodiscard]] virtual bool compile(ir::IRStream& stream) = 0;

    /**
     * @brief Sovereign MIR MachineFunction compilation.
     */
    [[nodiscard]] virtual bool compile_mir(const machine::MachineFunction& fn) {
        (void)fn;
        return false;
    }

    /**
     * @brief Universal ObjectFile compilation and loading.
     */
    [[nodiscard]] virtual bool compile_object(const dgtl::object::ObjectFile& obj) {
        (void)obj;
        return false;
    }

    /**
     * @brief Direct native execution of compiled machine function.
     */
    [[nodiscard]] virtual int64_t execute(const machine::MachineFunction& fn, void* ctx = nullptr) {
        void* ptr = get_function_pointer(fn.name());
        if (!ptr) return -1;
        using NativeFn = int64_t (*)(void*);
        return reinterpret_cast<NativeFn>(ptr)(ctx);
    }

    /**
     * @brief Resolve a symbol address by name.
     */
    [[nodiscard]] virtual uint64_t get_symbol_address(const char* symbol_name) noexcept = 0;

    [[nodiscard]] uint64_t get_symbol_address(std::string_view symbol_name) noexcept {
        return get_symbol_address(std::string(symbol_name).c_str());
    }

    [[nodiscard]] uint64_t get_symbol_address(const std::string& symbol_name) noexcept {
        return get_symbol_address(symbol_name.c_str());
    }

    [[nodiscard]] void* get_function_pointer(const char* symbol_name) noexcept {
        return reinterpret_cast<void*>(get_symbol_address(symbol_name));
    }

    [[nodiscard]] void* get_function_pointer(std::string_view symbol_name) noexcept {
        return get_function_pointer(std::string(symbol_name).c_str());
    }

    [[nodiscard]] void* get_function_pointer(const std::string& symbol_name) noexcept {
        return get_function_pointer(symbol_name.c_str());
    }

    template <class Fn>
    [[nodiscard]] Fn resolve_function(const char* symbol_name) noexcept {
        static_assert(std::is_pointer_v<Fn> && std::is_function_v<std::remove_pointer_t<Fn>>, "Fn must be a function-pointer type");
        return reinterpret_cast<Fn>(get_symbol_address(symbol_name));
    }

    virtual void register_host_symbol(const std::string& symbol_name, uint64_t address) = 0;
    virtual void register_host_symbol_slot(uint32_t slot, uint64_t address) = 0;

    [[nodiscard]] static std::unique_ptr<ExecutionEngine> create(Context& context);

protected:
    ExecutionEngine() = default;
    ExecutionEngine(const ExecutionEngine&) = delete;
    ExecutionEngine& operator=(const ExecutionEngine&) = delete;
};

} // namespace api
} // namespace dgtl
