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

// ============================================================================
// SDK / EXTERNAL USE ONLY
//
// This header is the public umbrella include for SDK consumers and demo code.
// Internal DGTL source files must NOT include this header — they must include
// only the precise headers they need. Violating this rule creates hidden
// coupling and significantly increases incremental build times.
//
// Acceptable callers:
//   - SDK implementation (dgtl_sdk_impl.cpp)
//   - Demo / sample programs
//   - External integrators that want a single include
//
// Not acceptable callers:
//   - Any header inside include/dgtl/
//   - Any .cpp in src/ other than the SDK and entry-point main
// ============================================================================

// ============================================================================
// UTILITIES (foundational - must be first)
// ============================================================================
#include "dgtl/utils/assert/Assertions.hpp"
#include "dgtl/utils/bit/Bit_Pack.hpp"
#include "dgtl/utils/hashing/Hashing.hpp"
#include "dgtl/utils/container/Flat_Map.hpp"
#include "dgtl/utils/automata/LUT_FSM_Matcher.hpp"
#include "dgtl/utils/memory/Memory_Guard.hpp"

#include "dgtl/runtime/Runtime_Intrinsics.hpp"

// ============================================================================
// CORE (foundational compiler infrastructure)
// ============================================================================
#include "dgtl/core/ABI.hpp"
#include "dgtl/core/MemoryCapability.hpp"
#include "dgtl/core/VirtualMemory.hpp"
#include "dgtl/core/MemoryManager.hpp"
#include "dgtl/core/NUMA.hpp"
#include "dgtl/core/Arena.hpp"
#include "dgtl/core/Compiler_Context.hpp"
#include "dgtl/core/ControlFlow.hpp"
#include "dgtl/ir/debug/debug_Metadata.hpp"
#include "dgtl/core/Function.hpp"
#include "dgtl/semantic/Symbol_Table.hpp"
#include "dgtl/core/Target.hpp"
#include "dgtl/core/Verified_Formal.hpp"

// ============================================================================
// BACKEND (lowering, driver, linker - code generation infrastructure)
// ============================================================================
#include "dgtl/backend/driver/Backend_Driver_API.hpp"
#include "dgtl/backend/lowering/ABI_Lowering.hpp"
#include "dgtl/backend/lowering/Stack_Spiller.hpp"

// ============================================================================
// IR (low-level to umbrella - foundational)
// ============================================================================
#include "dgtl/ir/base/IR_Enums.hpp"
#include "dgtl/ir/base/IR_Node.hpp"
#include "dgtl/ir/base/IR_Traits_Static.hpp"
#include "dgtl/ir/base/IR_Storage.hpp"
#include "dgtl/ir/base/IR_Stream.hpp"
#include "dgtl/ir/gen/DGTL_Gen_Ops.hpp"
#include "dgtl/ir/evolver/IR_Evolver_Mutation.hpp"
#include "dgtl/ir/runtime/TLS_Flush_Merge.hpp"
#include "dgtl/ir/Global_IR.hpp"

// IR Bridges (connect IR to other subsystems)
#include "dgtl/ir/interop/IR_Analysis_Bridge.hpp"

// ============================================================================
// FRONTEND INTEGRATION (public API boundary)
// ============================================================================
#include "dgtl/api/Api_all.hpp"

// ============================================================================
// ANALYSIS (data flow, profiling, cost modeling)
// ============================================================================
#include "dgtl/analysis/Analysis_Interface.hpp"
#include "dgtl/analysis/AI_Cost_Model.hpp"
#include "dgtl/analysis/Liveness_Memory_Dependency_Unified.hpp"
#include "dgtl/analysis/Polyhedral_Scop_Unified.hpp"
#include "dgtl/analysis/Speculative_Value_Profiler.hpp"

// ============================================================================
// GRAPH (program structure representation)
// ============================================================================
#include "dgtl/graph/IR_GraphEdgeClusterRegion.hpp"

// ============================================================================
// PASS (optimization transforms)
// ============================================================================
// Transformation passes (consolidated)
#include "dgtl/pass/transform/Transform_Scalar_Core.hpp"
#include "dgtl/pass/transform/Transform_Poly_Loop.hpp"
#include "dgtl/pass/transform/Transform_Control_Predication.hpp"
#include "dgtl/pass/transform/Transform_Inlining.hpp"

// Analysis passes
#include "dgtl/pass/analysis/Escape_Analysis.hpp"

// SIMD passes (unified header: consolidates 4 related optimization modules)
#include "dgtl/pass/simd/SimdGatherScatter_Legal_Vector_Recognizer.hpp"

// Memory passes
// Redundant store fusion handled by DSL-generated pass infrastructure

// Error handling passes
#include "dgtl/pass/error_handling/Enhanced_Error_Recovery.hpp"

// Speculative passes
#include "dgtl/pass/speculative/Speculate_Alias_DCE_Inliner_Temporal_Vector_Fusion.hpp"

// ML passes
#include "dgtl/pass/ml/MLoptimization.hpp"

// Query passes
#include "dgtl/pass/query/Query_Optimizer_Unified.hpp"

// Tuning passes
#include "dgtl/pass/tuning/Tuning_Optimize.hpp"

// Pass infrastructure
#include "dgtl/pipeline/GPU_Scheduling.hpp"

// ============================================================================
// PIPELINE (execution modes and pass management)
// ============================================================================
#include "dgtl/pipeline/Pass_Manager.hpp"
#include "dgtl/pipeline/Pipeline_Probes.hpp"
#include "dgtl/pipeline/Performance_Probes.hpp"

// ============================================================================
// ISA (instruction selection, lowering, register allocation, emission)
// ============================================================================
// Target definitions
#include "dgtl/isa/target/Wasm_Target.hpp"
#include "dgtl/isa/target/Wasm_Encoder.hpp"

// Register allocation
#include "dgtl/isa/regalloc/ARM64_RegAlloc.hpp"
#include "dgtl/isa/regalloc/Strike_Live_Manager.hpp"
#include "dgtl/isa/regalloc/X64_Reg_Map.hpp"
#include "dgtl/isa/regalloc/X64_GraphColoring_RegAlloc.hpp"

// ISA lowering (backend lowering)
// [Note: Includes moved to BACKEND section above]
#include "dgtl/isa/lowering/Instruction_Selector.hpp"
#include "dgtl/isa/lowering/Legalizer.hpp"
#include "dgtl/isa/lowering/X64_Machine_Form_Lowering.hpp"

// Code emission (backends now use DSL-generated IR metadata)
#include "dgtl/isa/emitter/ARM64_Emitter.hpp"
#include "dgtl/isa/lowering/X64_Machine_Form_Lowering.hpp"
#include "dgtl/isa/emitter/MachO_Emitter.hpp"
#include "dgtl/isa/emitter/NASM_Emitter.hpp"
#include "dgtl/ir/gen/DGTL_Gen_Ops.hpp"

// Cross-backend feature implementations (v1.0.2)
#include "dgtl/isa/emitter/Arm32neon_RiscV64_WasmGSFMA.hpp"

// Linking
#include "dgtl/backend/linker/Linker_Backend.hpp"

// [Note: ABI bridge moved to BACKEND section above]

// ============================================================================
// EXECUTION (binary loading, code caching, dispatching)
// ============================================================================
#include "dgtl/execution/CasheSendBinary.hpp"

// ============================================================================
// RUNTIME (task scheduling, context management)
// ============================================================================
#include "dgtl/runtime/TaskScheduler.hpp"
#include "dgtl/runtime/Runtime_Context.hpp"

// ============================================================================
// JIT (just-in-time compilation)
// ============================================================================
// Orchestration
#include "dgtl/jit/JIT_Orchestrator.hpp"

// Runtime components
#include "dgtl/jit/Hardware_Scheduler.hpp"
#include "dgtl/jit/JIT_Executor.hpp"

// Speculation, Deoptimization, and Tiered Compilation (unified)
#include "dgtl/jit/Async_Dopt_Tiered.hpp"



