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

#include <memory>

export module dgtl.internal;

// --- CORE SYSTEM RE-EXPORTS ---
export import dgtl.ir_core;

// --- API PARTITION AGGREGATION ---
export import dgtl.internal.api_execution_engine;
export import dgtl.internal.api_runtime;
export import dgtl.internal.api_ir_builder;
export import dgtl.internal.api_jit_session;
export import dgtl.internal.api_hybrid_session;
export import dgtl.internal.api_aot_session;
export import dgtl.internal.api_abi;
export import dgtl.internal.api_abi_lowering;
export import dgtl.internal.api_backend_driver_api;
export import dgtl.internal.api_backend_driver_shared_utils;
export import dgtl.config;

// Import local build dependency pre-requisites
import dgtl.compiler_context;

export namespace dgtl::api {

/**
 * @brief Factory function for session creation.
 * Resolves the requested execution mode and returns the appropriate implementation.
 */
std::unique_ptr<ExecutionEngine> ExecutionEngine::create(Context& context) noexcept {
    // Note: Assuming ExecutionMode is defined within dgtl::api or accessible via Context
    switch (context.get_config().mode) {
        case ExecutionMode::JIT:
            return std::make_unique<JITSession>(context);
        case ExecutionMode::AOT:
            return std::make_unique<AOTSession>(context);
        case ExecutionMode::Adaptive:
        default:
            return std::make_unique<HybridSession>(context);
    }
}

} // namespace dgtl::api