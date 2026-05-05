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

#include "dgtl/api/Config.hpp"
#include <memory>
#include <utility>

namespace dgtl::core {
    class DGTLContext;  // Forward declaration (opaque internal type)
}

namespace dgtl {
namespace api {

/**
 * @class Context
 * @brief Public API wrapper for DGTL compilation context.
 * 
 * Provides an opaque handle to internal DGTLContext with move semantics.
 * Not copyable: each Context represents a unique compilation lifetime.
 */
class Context {
public:
    /**
     * @brief Create a new compilation context with given configuration.
     * @param config Configuration controlling execution and optimization
     * @return A new Context handle
     */
    static Context create(const Config& config = default_config()) noexcept;

    /**
     * @brief Default constructor: creates invalid context.
     */
    Context() noexcept;

    /**
     * @brief Destructor: releases internal implementation.
     */
    ~Context() noexcept;

    /**
     * @brief Move constructor: takes ownership of implementation from other.
     */
    Context(Context&& other) noexcept;

    /**
     * @brief Move assignment: transfers ownership.
     */
    Context& operator=(Context&& other) noexcept;

    /**
     * @brief Deleted copy constructor: contexts are not copyable.
     */
    Context(const Context&) = delete;

    /**
     * @brief Deleted copy assignment: contexts are not copyable.
     */
    Context& operator=(const Context&) = delete;

    /**
     * @brief Check if this context is valid.
     * @return true if context holds a valid implementation
     */
    [[nodiscard]] explicit operator bool() const noexcept;

    /**
     * @brief Check validity.
     * @return true if context is valid
     */
    [[nodiscard]] bool is_valid() const noexcept;

    /**
     * @brief Get the configuration used by this context.
     * @return Reference to the Config (valid only if context is valid)
     */
    [[nodiscard]] const Config& get_config() const noexcept;

    /**
     * @brief Access internal DGTLContext (friend classes only).
     * For internal use by ExecutionEngine and other API classes.
     */
    [[nodiscard]] dgtl::core::DGTLContext* get_impl() noexcept;
    [[nodiscard]] const dgtl::core::DGTLContext* get_impl() const noexcept;

private:
    friend class ExecutionEngine;
    friend class JITSession;
    friend class AOTSession;
    friend class HybridSession;
    friend class IRBuilder;

    std::unique_ptr<dgtl::core::DGTLContext> impl_;
    Config config_;

    explicit Context(std::unique_ptr<dgtl::core::DGTLContext> impl, 
                     const Config& cfg) noexcept;
};

} // namespace api
} // namespace dgtl
