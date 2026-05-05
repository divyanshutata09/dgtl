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

#include <cstdint>
#include <memory>
#include <functional>
#include <mutex>

namespace dgtl {
namespace execution {

enum class DispatchTarget : uint8_t {
    X64_Native = 0,
    ARM64_Native = 1,
    WASM_Interpreter = 2,
    WASM_JIT = 3,
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
    Execution_Dispatcher() = default;
    ~Execution_Dispatcher() = default;

    using ExecutionFunc = std::function<int64_t(void*)>;

    void set_executor(DispatchTarget target, ExecutionFunc func) noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        uint8_t idx = static_cast<uint8_t>(target);
        if (idx < static_cast<uint8_t>(DispatchTarget::Count)) {
            executors_[idx] = func;
        }
    }

    /**
     * @brief Dispatch execution to registered executor.
     * @param target Target ISA/executor to use
     * @param context Opaque execution context
     * @return Result code (negative = error, >= 0 = success)
     */
    [[nodiscard]] int64_t dispatch(DispatchTarget target, void* context) const noexcept {
        if (!context) return static_cast<int64_t>(DispatchError::NullContext);
        
        uint8_t idx = static_cast<uint8_t>(target);
        if (idx >= static_cast<uint8_t>(DispatchTarget::Count)) {
            return static_cast<int64_t>(DispatchError::InvalidTarget);
        }
        
        std::lock_guard<std::mutex> lock(mutex_);
        if (!executors_[idx]) {
            return static_cast<int64_t>(DispatchError::NoExecutor);
        }
        
        try {
            return executors_[idx](context);
        } catch (...) {
            return static_cast<int64_t>(DispatchError::ExecutionFailed);
        }
    }

    /**
     * @brief Check if executor is registered for target.
     */
    [[nodiscard]] bool has_executor(DispatchTarget target) const noexcept {
        uint8_t idx = static_cast<uint8_t>(target);
        if (idx >= static_cast<uint8_t>(DispatchTarget::Count)) return false;
        std::lock_guard<std::mutex> lock(mutex_);
        return static_cast<bool>(executors_[idx]);
    }

private:
    ExecutionFunc executors_[static_cast<size_t>(DispatchTarget::Count)] = {};
    mutable std::mutex mutex_;
};

} // namespace execution
} // namespace dgtl
