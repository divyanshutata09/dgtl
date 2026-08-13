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

/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:      DGTL (Deep General-purpose Transformation Layer)
 * Owner:        Divyanshu Tata
 * Repository:   github.com/TataG-Systems/DGTL
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

/* * DGTL (Deep General-purpose Transformation Layer) version 1.0.0 (DGTL Edition)
 * THE UNIVERSAL IR LINKER (UIL)
 * -------------------------------------------------------------------------
 * ORDER OF INCLUSION IS CRITICAL FOR C++26 TEMPLATE RESOLUTION.
 */

module;

#include <cstdlib>
#include <atomic>
#include <thread>

export module dgtl.ir_stream;

// ============================================================================
// EXTERNAL STANDALONE PRIMARY MODULES (Imported & Re-Exported Flat)
// ============================================================================
import dgtl.config;                         // include\dgtl\api\Config.ixx
export import dgtl.target;                  // include\dgtl\core\Target.ixx
export import dgtl.numa;                    // include\dgtl\core\NUMA.ixx
export import dgtl.numa_runtime;            // include\dgtl\runtime\NUMA_Runtime.ixx
export import dgtl.ir_enums;                // include\dgtl\ir\base\IR_Enums.ixx
export import dgtl.ir_node;                 // include\dgtl\ir\base\IR_Node.ixx
export import dgtl.virtual_memory;          // include\dgtl\core\VirtualMemory.ixx
export import dgtl.memory_capability;       // include\dgtl\core\MemoryCapability.ixx
export import dgtl.memory_manager;          // include\dgtl\core\MemoryManager.ixx
export import dgtl.arena;                   // include\dgtl\core\Arena.ixx
export import dgtl.hashing;                 // include\dgtl\utils\hashing\Hashing.ixx
export import dgtl.sync;                    // 🚀 NATIVELY IMPORTED & FLAT RE-EXPORTED: Your high-performance spinlock layer
export import dgtl.ir_traits;               // include\dgtl\ir\base\IR_Traits_Static.ixx
export import dgtl.op_traits;               // include\dgtl\ir\interfaces\Op_Traits.ixx
export import dgtl.assertions;              // include\dgtl\utils\assert\Assertions.ixx
export import dgtl.flat_map;                // include\dgtl\utils\container\Flat_Map.ixx
export import dgtl.task_scheduler;          // include\dgtl\runtime\TaskScheduler.ixx
export import dgtl.runtime_context;         // include\dgtl\runtime\Runtime_Context.ixx
export import dgtl.compiler_context;        // include\dgtl\core\Compiler_Context.ixx
export import dgtl.dgtl_bulletins;          // include\dgtl\runtime\dgtl_bulletins.ixx
export import dgtl.runtime_intrinsics;       // include\dgtl\runtime\Runtime_Intrinsics.ixx
export import dgtl.memory_guard;            // include\dgtl\utils\memory\Memory_Guard.ixx
export import dgtl.bit_pack;                // include\dgtl\utils\bit\Bit_Pack.ixx
export import dgtl.ir_storage;              // include\dgtl\ir\base\IR_Storage.ixx
export import dgtl.testing.filecheck;       // include\dgtl\testing\FileCheck.ixx
export import dgtl.lut_fsm;                 // include\dgtl\utils\automata\LUT_FSM_Matcher.ixx

// ============================================================================
// DECOMPOSED SUB-MODULE INTERFACES (Prevents GCC Metadata Segfault)
// ============================================================================

// Re-exports: base, storage, verifier, validation, and analysis partitions
export import dgtl.ir_core;

// Re-exports: pass manager, pipelines, scalar/vector transforms, and optimization passes
export import dgtl.ir_passes;       // Pass tracking, managers, orchestrators, runtime flushes
export import dgtl.ir_transforms;   // Optimization matrices (SIMD, polyhedral, ML, loops)
export import dgtl.ir_backend;      // ISA instruction selectors, machine emitters, graph allocators
export import dgtl.ir_jit;          // JIT engine runtimes, tiered optimization schedulers, loaders