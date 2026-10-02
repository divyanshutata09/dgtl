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
#include <string_view>
#include <memory>
#include <span>

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
};

} // namespace dgtl::execution


