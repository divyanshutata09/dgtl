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
#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <chrono>

export module dgtl.execution.dispatcher;

import dgtl.mir;

export namespace dgtl::execution::dispatcher {

enum class DispatchTarget : uint8_t {
    X64_Native = 0,
    ARM64_Native = 1,
    WASM_Interpreter = 2,
    WASM_JIT = 3,
    WASM_ = WASM_JIT,
    PE_Executable = 4,
    ELF_Executable = 5,
    MachO_Executable = 6,
    ARM32_Native = 7,
    RiscV64_Native = 8,
    CUDA_Kernel = 9,
    ROCm_Kernel = 10,
    Count = 11
};

enum class DispatchError : int64_t {
    Success = 0,
    NullContext = -1,
    NoExecutor = -2,
    ExecutionFailed = -3,
    InvalidTarget = -4,
    FunctionNotResolved = -5
};

class Execution_Dispatcher {
public:
    static constexpr size_t kMaxDispatchTargets = 16;
    using ExecutionFunc = std::function<int64_t(void*)>;
    using RawExecutionFunc = int64_t (*)(void*);

    Execution_Dispatcher() = default;
    ~Execution_Dispatcher() = default;

    Execution_Dispatcher(const Execution_Dispatcher&) = delete;
    Execution_Dispatcher& operator=(const Execution_Dispatcher&) = delete;

    void set_raw_executor(DispatchTarget target, RawExecutionFunc raw_func) noexcept {
        const auto idx = static_cast<uint8_t>(target);
        if (idx < static_cast<uint8_t>(DispatchTarget::Count)) {
            raw_executors_[idx].store(raw_func, std::memory_order_release);
        }
    }

    void set_executor(DispatchTarget target, ExecutionFunc func) {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        const auto idx = static_cast<uint8_t>(target);
        if (idx < static_cast<uint8_t>(DispatchTarget::Count)) {
            executors_[idx] = std::move(func);
        }
    }

    void clear_executor(DispatchTarget target) {
        const auto idx = static_cast<uint8_t>(target);
        if (idx < static_cast<uint8_t>(DispatchTarget::Count)) {
            raw_executors_[idx].store(nullptr, std::memory_order_release);
            std::unique_lock<std::shared_mutex> lock(mutex_);
            executors_[idx] = nullptr;
        }
    }

    // Zero-overhead atomic fast-path dispatch (0 ns lock latency)
    [[nodiscard]] int64_t dispatch(DispatchTarget target, void* context = nullptr) const noexcept {
        const auto idx = static_cast<uint8_t>(target);
        if (idx >= static_cast<uint8_t>(DispatchTarget::Count)) [[unlikely]] {
            return static_cast<int64_t>(DispatchError::InvalidTarget);
        }

        RawExecutionFunc raw_fn = raw_executors_[idx].load(std::memory_order_acquire);
        if (raw_fn != nullptr) [[likely]] {
            invocation_counts_[idx].fetch_add(1, std::memory_order_relaxed);
            return raw_fn(context);
        }

        std::shared_lock<std::shared_mutex> lock(mutex_);
        if (!executors_[idx]) {
            return static_cast<int64_t>(DispatchError::NoExecutor);
        }

        invocation_counts_[idx].fetch_add(1, std::memory_order_relaxed);
        try {
            return executors_[idx](context);
        } catch (...) {
            return static_cast<int64_t>(DispatchError::ExecutionFailed);
        }
    }

    // Direct invocation by function pointer
    [[nodiscard]] static int64_t dispatch_pointer(const void* fn_ptr, void* context = nullptr) noexcept {
        if (!fn_ptr) return static_cast<int64_t>(DispatchError::NullContext);
        auto fn = reinterpret_cast<RawExecutionFunc>(const_cast<void*>(fn_ptr));
        return fn(context);
    }

    // Typed template direct invocation
    template <typename Ret, typename... Args>
    [[nodiscard]] static Ret invoke_typed(const void* fn_ptr, Args... args) {
        using FnType = Ret (*)(Args...);
        auto fn = reinterpret_cast<FnType>(const_cast<void*>(fn_ptr));
        return fn(args...);
    }

    [[nodiscard]] bool has_raw_executor(DispatchTarget target) const noexcept {
        const auto idx = static_cast<uint8_t>(target);
        if (idx >= static_cast<uint8_t>(DispatchTarget::Count)) {
            return false;
        }
        return raw_executors_[idx].load(std::memory_order_relaxed) != nullptr;
    }

    [[nodiscard]] bool has_executor(DispatchTarget target) const noexcept {
        const auto idx = static_cast<uint8_t>(target);
        if (idx >= static_cast<uint8_t>(DispatchTarget::Count)) {
            return false;
        }

        if (has_raw_executor(target)) {
            return true;
        }

        std::shared_lock<std::shared_mutex> lock(mutex_);
        return static_cast<bool>(executors_[idx]);
    }

    [[nodiscard]] size_t invocation_count(DispatchTarget target) const noexcept {
        const auto idx = static_cast<uint8_t>(target);
        if (idx >= static_cast<uint8_t>(DispatchTarget::Count)) return 0;
        return invocation_counts_[idx].load(std::memory_order_relaxed);
    }

private:
    alignas(64) std::array<std::atomic<RawExecutionFunc>, kMaxDispatchTargets> raw_executors_{};
    alignas(64) mutable std::shared_mutex mutex_;
    alignas(64) std::array<ExecutionFunc, kMaxDispatchTargets> executors_{};
    alignas(64) mutable std::array<std::atomic<size_t>, kMaxDispatchTargets> invocation_counts_{};
};

} // namespace dgtl::execution::dispatcher


