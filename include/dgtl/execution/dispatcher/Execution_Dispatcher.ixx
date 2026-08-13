/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository: github.com/TataG-Systems/DGTL
 */

module;

#include <cstdlib>

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <shared_mutex>

export module dgtl.ir_core.execution_dispatcher;

export namespace dgtl::execution::dispatcher {

enum class DispatchTarget : uint8_t {
    X64_Native = 0,
    ARM64_Native = 1,
    WASM_Interpreter = 2,
    WASM_JIT = 3,
    WASM_ = WASM_JIT,
    Count = 4
};

enum class DispatchError : int64_t {
    Success = 0,
    NullContext = -1,
    NoExecutor = -2,
    ExecutionFailed = -3,
    InvalidTarget = -4
};

class Execution_Dispatcher {
public:
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

    [[nodiscard]] int64_t dispatch(DispatchTarget target, void* context) const noexcept {
        if (context == nullptr) [[unlikely]] {
            return static_cast<int64_t>(DispatchError::NullContext);
        }

        const auto idx = static_cast<uint8_t>(target);
        if (idx >= static_cast<uint8_t>(DispatchTarget::Count)) [[unlikely]] {
            return static_cast<int64_t>(DispatchError::InvalidTarget);
        }

        // 🚀 ZERO-LOCK FAST PATH: Atomic raw function pointer execution (0 ns latency)
        RawExecutionFunc raw_fn = raw_executors_[idx].load(std::memory_order_acquire);
        if (raw_fn != nullptr) [[likely]] {
            return raw_fn(context);
        }

        std::shared_lock<std::shared_mutex> lock(mutex_);
        if (!executors_[idx]) {
            return static_cast<int64_t>(DispatchError::NoExecutor);
        }

        try {
            return executors_[idx](context);
        } catch (...) {
            return static_cast<int64_t>(DispatchError::ExecutionFailed);
        }
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

private:
    alignas(64) std::array<std::atomic<RawExecutionFunc>, static_cast<size_t>(DispatchTarget::Count)> raw_executors_{};
    alignas(64) mutable std::shared_mutex mutex_;
    alignas(64) std::array<ExecutionFunc, static_cast<size_t>(DispatchTarget::Count)> executors_{};
};

} // namespace dgtl::execution::dispatcher