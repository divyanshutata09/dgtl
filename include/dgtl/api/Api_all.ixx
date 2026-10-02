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

export module dgtl.api;

// --- CORE SYSTEM RE-EXPORTS ---
export import dgtl.ir;
export import dgtl.mir;
export import dgtl.object.file;
export import dgtl.execution;
export import dgtl.formal;

// --- API PARTITION AGGREGATION ---
export import dgtl.api.execution_engine;
export import dgtl.api.runtime;
export import dgtl.api.ir_builder;
export import dgtl.api.jit_session;
export import dgtl.api.hybrid_session;
export import dgtl.api.aot_session;
export import dgtl.api.abi;
export import dgtl.api.abi_lowering;
export import dgtl.api.backend_driver;
export import dgtl.api.backend_driver_shared;
export import dgtl.api.config;

import dgtl.compiler_context;

export namespace dgtl::api {

/**
 * @brief Factory function for session creation.
 * Resolves the requested execution mode and returns the appropriate implementation.
 */
std::unique_ptr<ExecutionEngine> ExecutionEngine::create(Context& context) {
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
