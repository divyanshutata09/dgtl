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

// Global Module Fragment
#include <cstdint>

export module dgtl.ir_core.fwd;

export namespace dgtl::ir {

    // Core IR interfaces
    class IRStream;

    // Profiling and backend metadata
    struct BackendProfile;
    struct ProfilingData;

    // CFG primitives
    struct IRBasicBlock;
    struct CFGEdge;

    // Stable enum ABI
    enum class CFGEdgeKind : std::uint8_t;

}