/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:    DGTL (Deep General-purpose Transformation Layer)
 * Component:  Compiler Context Implementation
 * Purpose:    Runtime context management for frontend IR construction
 */

#include "dgtl/core/Compiler_Context.hpp"
#include "dgtl/core/Arena.hpp"

namespace dgtl {
namespace api {

// ============================================================================
// Context Factory & Lifecycle
// ============================================================================

Context Context::create(const Config& config) noexcept {
    // Allocate a global arena with configuration-based thread count and memory.
    // Default: 8 threads, 256MB per thread (tuned for typical frontend compilation).
    auto arena = std::make_unique<core::DGTL_Arena>(
        8,                    // num_threads
        256                   // size_mb_per_thread
    );

    if (!arena) {
        return Context();  // Return invalid context on allocation failure
    }

    // Create the internal DGTLContext with the arena
    auto internal_ctx = std::make_unique<core::DGTLContext>(*arena);
    if (!internal_ctx) {
        return Context();  // Return invalid context on context creation failure
    }

    // Package both arena and context into the public wrapper
    Context ctx;
    ctx.arena_ = std::move(arena);
    ctx.impl_ = std::move(internal_ctx);
    ctx.config_ = config;
    return ctx;
}

Context::Context() noexcept
    : arena_(nullptr), impl_(nullptr), config_(default_config()) {}

Context::~Context() noexcept {
    impl_.reset();
    arena_.reset();
}

Context::Context(Context&& other) noexcept
    : arena_(std::move(other.arena_)),
      impl_(std::move(other.impl_)),
      config_(other.config_) {}

Context& Context::operator=(Context&& other) noexcept {
    if (this != &other) {
        arena_ = std::move(other.arena_);
        impl_ = std::move(other.impl_);
        config_ = other.config_;
    }
    return *this;
}

Context::operator bool() const noexcept {
    return impl_ != nullptr && arena_ != nullptr;
}

bool Context::is_valid() const noexcept {
    return impl_ != nullptr && arena_ != nullptr;
}

const Config& Context::get_config() const noexcept {
    return config_;
}

core::DGTLContext* Context::get_impl() noexcept {
    return impl_.get();
}

const core::DGTLContext* Context::get_impl() const noexcept {
    return impl_.get();
}

} // namespace api
} // namespace dgtl
