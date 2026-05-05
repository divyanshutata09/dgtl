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
#include <unordered_map>
#include <memory>
#include <functional>
#include <vector>

namespace dgtl {
namespace jit {

/**
 * @enum DeoptReason
 * @brief Reasons why deoptimization was triggered.
 */
enum class DeoptReason : uint8_t {
    SpeculationFailed = 0,  ///< Speculative assumption proved wrong
    TypeMismatch = 1,       ///< Type guard failed at runtime
    BoundCheckFailed = 2,   ///< Array bounds check failed
    GuardFailed = 3,        ///< Generic guard condition failed
    Explicit = 4            ///< Explicit deoptimization requested
};

/**
 * @struct DeoptInfo
 * @brief Metadata about a deoptimization point.
 */
struct DeoptInfo {
    uint32_t id;
    DeoptReason reason;
    const char* guard_name;
    uint64_t trigger_count;
};

/**
 * @class Deopt_Handler
 * @brief Manages deoptimization callbacks and fallback execution paths.
 * 
 * When JIT-compiled code encounters a speculative assumption that fails,
 * it deoptimizes: invalidates the optimized code and falls back to
 * interpreter or AOT-compiled path with updated type information.
 */
class Deopt_Handler {
public:
    Deopt_Handler() = default;
    ~Deopt_Handler() = default;

    Deopt_Handler(const Deopt_Handler&) = delete;
    Deopt_Handler& operator=(const Deopt_Handler&) = delete;

    using DeoptCallback = std::function<int64_t(DeoptReason, void*)>;

    /**
     * @brief Register a deoptimization handler for a specific ID.
     * @param id Unique deoptimization point ID
     * @param handler Callback to invoke when deoptimization occurs
     */
    void register_deopt_handler(uint32_t id, DeoptCallback handler) noexcept {
        if (handler) {
            handlers_[id] = handler;
        }
    }

    /**
     * @brief Unregister a deoptimization handler.
     * @param id Handler ID to remove
     */
    void unregister_deopt_handler(uint32_t id) noexcept {
        handlers_.erase(id);
    }

    /**
     * @brief Handle a deoptimization event.
     * @param id Deoptimization point ID
     * @param reason Why deoptimization occurred
     * @param context Opaque context passed to handler
     * @return Result code from handler (negative = error)
     */
    [[nodiscard]] int64_t handle_deopt(uint32_t id, DeoptReason reason, void* context) noexcept {
        auto it = handlers_.find(id);
        if (it == handlers_.end()) return -1;
        
        try {
            // Track deoptimization event
            deopt_history_.push_back({id, reason, nullptr, 0});
            
            // Invoke callback
            return it->second(reason, context);
        } catch (...) {
            return -1;
        }
    }

    /**
     * @brief Check if a handler is registered.
     * @param id Handler ID
     * @return true if handler exists
     */
    [[nodiscard]] bool has_handler(uint32_t id) const noexcept {
        return handlers_.count(id) > 0;
    }

    /**
     * @brief Get the count of registered handlers.
     * @return Number of active handlers
     */
    [[nodiscard]] size_t handler_count() const noexcept {
        return handlers_.size();
    }

    /**
     * @brief Get deoptimization history.
     * @return Reference to history log
     */
    [[nodiscard]] const std::vector<DeoptInfo>& get_history() const noexcept {
        return deopt_history_;
    }

    /**
     * @brief Clear all handlers and reset state.
     */
    void clear() noexcept {
        handlers_.clear();
        deopt_history_.clear();
    }

private:
    std::unordered_map<uint32_t, DeoptCallback> handlers_;
    std::vector<DeoptInfo> deopt_history_;
};

} // namespace jit
} // namespace dgtl
