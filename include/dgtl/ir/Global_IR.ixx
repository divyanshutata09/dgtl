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

/**
 * @file Global_IR.ixx
 * @brief Universal IR Linker (UIL) and primary structural aggregator.
 * @note Order of inclusion is critical for C++26 template resolution.
 */

module;

#include <cstdlib>
#include <atomic>
#include <thread>

export module dgtl.ir_stream;

// ============================================================================
// EXTERNAL STANDALONE PRIMARY MODULES
// ============================================================================
import dgtl.config;
export import dgtl.target;
export import dgtl.numa;
export import dgtl.numa_runtime;
export import dgtl.ir_enums;
export import dgtl.ir_node;
export import dgtl.virtual_memory;
export import dgtl.memory_capability;
export import dgtl.memory_manager;
export import dgtl.arena;
export import dgtl.hashing;
export import dgtl.sync;                 // High-performance spinlock layer
export import dgtl.ir_traits;
export import dgtl.op_traits;
export import dgtl.assertions;
export import dgtl.flat_map;
export import dgtl.task_scheduler;
export import dgtl.runtime_context;
export import dgtl.compiler_context;
export import dgtl.dgtl_bulletins;
export import dgtl.runtime_intrinsics;
export import dgtl.memory_guard;
export import dgtl.bit_pack;
export import dgtl.ir_storage;
export import dgtl.testing.filecheck;
export import dgtl.lut_fsm;

// ============================================================================
// DECOMPOSED SUB-MODULE INTERFACES
// ============================================================================
export import dgtl.ir_core;
export import dgtl.ir_passes;       // Pass tracking, managers, orchestrators, runtime flushes
export import dgtl.ir_transforms;   // Optimization matrices (SIMD, polyhedral, ML, loops)
export import dgtl.ir_backend;      // ISA instruction selectors, machine emitters, graph allocators
export import dgtl.ir_jit;          // JIT engine runtimes, tiered optimization schedulers, loaders