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

#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

export module dgtl.execution;

export import dgtl.execution.cache;
export import dgtl.execution.dispatcher;
export import dgtl.execution.loader;

import dgtl.mir;
import dgtl.object.file;

export namespace dgtl::execution {

class ExecutionCoordinator {
    cache::Code_Cache code_cache_;
    dispatcher::Execution_Dispatcher dispatcher_;
    loader::Binary_Loader loader_;

public:
    ExecutionCoordinator(size_t cache_capacity = 64 * 1024 * 1024)
        : code_cache_(cache_capacity) {}

    [[nodiscard]] cache::Code_Cache& cache() noexcept { return code_cache_; }
    [[nodiscard]] const cache::Code_Cache& cache() const noexcept { return code_cache_; }

    [[nodiscard]] dispatcher::Execution_Dispatcher& dispatcher() noexcept { return dispatcher_; }
    [[nodiscard]] const dispatcher::Execution_Dispatcher& dispatcher() const noexcept { return dispatcher_; }

    [[nodiscard]] loader::Binary_Loader& loader() noexcept { return loader_; }
    [[nodiscard]] const loader::Binary_Loader& loader() const noexcept { return loader_; }

    // Store compiled code in cache
    [[nodiscard]] bool load_and_cache(std::string_view name, const void* code, size_t size, bool executable = true) {
        return code_cache_.store(name, code, size, executable);
    }

    [[nodiscard]] bool load_and_cache(const machine::MachineFunction& fn, const void* code, size_t size, bool executable = true) {
        return code_cache_.store(fn, code, size, executable);
    }

    [[nodiscard]] bool load_and_cache(std::string_view name, std::span<const uint8_t> code, bool executable = true) {
        return code_cache_.store(name, code, executable);
    }

    [[nodiscard]] bool load_and_cache(const machine::MachineFunction& fn, std::span<const uint8_t> code, bool executable = true) {
        return code_cache_.store(fn, code, executable);
    }

    // Execute compiled code pointer directly
    [[nodiscard]] int64_t execute_native(const void* fn_ptr, void* ctx = nullptr) const noexcept {
        return dispatcher::Execution_Dispatcher::dispatch_pointer(fn_ptr, ctx);
    }

    // Execute cached function by name
    [[nodiscard]] int64_t execute(std::string_view name, void* ctx = nullptr) const {
        const void* ptr = code_cache_.retrieve(name);
        if (!ptr) {
            return static_cast<int64_t>(dispatcher::DispatchError::FunctionNotResolved);
        }
        return execute_native(ptr, ctx);
    }

    // Execute machine function if already compiled and cached
    [[nodiscard]] int64_t execute(const machine::MachineFunction& fn, void* ctx = nullptr) const {
        return execute(fn.name(), ctx);
    }

    // Typed direct invocation by cached name
    template <typename Ret = int64_t, typename... Args>
    [[nodiscard]] Ret invoke(std::string_view name, Args... args) const {
        const void* ptr = code_cache_.retrieve(name);
        if (!ptr) {
            throw std::runtime_error("ExecutionCoordinator: symbol not found in code cache: " + std::string(name));
        }
        return dispatcher::Execution_Dispatcher::invoke_typed<Ret>(ptr, args...);
    }

    template <typename Ret = int64_t, typename... Args>
    [[nodiscard]] Ret invoke(const machine::MachineFunction& fn, Args... args) const {
        return invoke<Ret>(fn.name(), args...);
    }

    [[nodiscard]] bool is_cached(std::string_view name) const noexcept {
        return code_cache_.exists(name);
    }

    [[nodiscard]] bool is_cached(const machine::MachineFunction& fn) const noexcept {
        return code_cache_.exists(fn);
    }

    void clear() {
        code_cache_.clear();
    }
};

} // namespace dgtl::execution


