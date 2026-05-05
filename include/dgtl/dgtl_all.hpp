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
// UTILITIES (foundational - must be first)
// ============================================================================
#include "dgtl/utils/Assertions.hpp"
#include "dgtl/utils/Bit_Pack.hpp"
#include "dgtl/utils/Hashing.hpp"

// ============================================================================
// CORE (foundational compiler infrastructure)
// ============================================================================
#include "dgtl/core/ABI.hpp"
#include "dgtl/core/Arena.hpp"
#include "dgtl/core/Context.hpp"
#include "dgtl/core/ControlFlow.hpp"
#include "dgtl/core/Debug_Metadata.hpp"
#include "dgtl/core/Function.hpp"
#include "dgtl/core/FrontendHelpers.hpp"
#include "dgtl/core/Frontend_Payload.hpp"
#include "dgtl/core/Memory_Guard.hpp"
#include "dgtl/core/Symbol_Table.hpp"
#include "dgtl/core/Target.hpp"
#include "dgtl/core/Verified_Formal.hpp"
#include "dgtl/core/Backend_Driver_API.hpp"

// ============================================================================
// IR (low-level to umbrella - foundational)
// ============================================================================
#include "dgtl/ir/base/IR_Types.hpp"
#include "dgtl/ir/base/IR_Nodes.hpp"
#include "dgtl/ir/base/IR_Traits.hpp"
#include "dgtl/ir/base/IR_Storage.hpp"
#include "dgtl/ir/base/IR_Stream.hpp"
#include "dgtl/ir/dialect/IR_Dialect_Interface.hpp"
#include "dgtl/ir/evolution/Mutation_Strategies.hpp"
#include "dgtl/ir/evolution/IR_Evolver.hpp"
#include "dgtl/ir/runtime/TLS_Flush_Merge.hpp"
#include "dgtl/ir/Global_IR.hpp"

// IR Bridges (connect IR to other subsystems)
#include "dgtl/ir/bridge/IR_Analysis_Bridge.hpp"
#include "dgtl/ir/bridge/IR_Codegen_Bridge.hpp"
#include "dgtl/ir/bridge/IR_Execution_Bridge.hpp"

// ============================================================================
// FRONTEND INTEGRATION (public API boundary)
// ============================================================================
#include "dgtl/api/Api_all.hpp"

// ============================================================================
// ANALYSIS (data flow, profiling, cost modeling)
// ============================================================================
#include "dgtl/analysis/interface/Analysis_Interface.hpp"
#include "dgtl/analysis/cost/AI_Cost_Model.hpp"
#include "dgtl/analysis/dataflow/Liveness_Analyzer.hpp"
#include "dgtl/analysis/dataflow/Memory_Dependency_Smasher.hpp"
#include "dgtl/analysis/polyhedral/Scop_Detector.hpp"
#include "dgtl/analysis/polyhedral/Polyhedral_Schedule.hpp"
#include "dgtl/analysis/polyhedral/Scop_Transformer.hpp"
#include "dgtl/analysis/profiling/Speculative_Value_Profiler.hpp"

// ============================================================================
// GRAPH (program structure representation)
// ============================================================================
#include "dgtl/graph/Dependency_Edge.hpp"
#include "dgtl/graph/Execution_Graph.hpp"
#include "dgtl/graph/Fusion_Cluster.hpp"
#include "dgtl/graph/Region.hpp"

// ============================================================================
// PASS (optimization transforms)
// ============================================================================
// Transformation passes
#include "dgtl/pass/transform/DCE_Solver.hpp"
#include "dgtl/pass/transform/GVN.hpp"
#include "dgtl/pass/transform/Instruction_Folder.hpp"
#include "dgtl/pass/transform/Instruction_Fusion_Synth.hpp"
#include "dgtl/pass/transform/Instruction_Reordering.hpp"
#include "dgtl/pass/transform/Loop_Unswitcher.hpp"
#include "dgtl/pass/transform/Masked_Predication_Lowering.hpp"
#include "dgtl/pass/transform/Poly_Transform.hpp"
#include "dgtl/pass/transform/Aggressive_Inliner.hpp"
#include "dgtl/pass/transform/Loop_Fusion.hpp"

// Analysis passes
#include "dgtl/pass/analysis/Escape_Analysis.hpp"

// SIMD passes
#include "dgtl/pass/simd/SIMD_Legalizer.hpp"
#include "dgtl/pass/simd/SIMD_Gather_Scatter_Optimizer.hpp"
#include "dgtl/pass/simd/Advanced_SIMD_Recognizer.hpp"
#include "dgtl/pass/simd/Advanced_Vectorization.hpp"

// Memory passes
#include "dgtl/pass/memory/Redundant_Store_Fuser.hpp"

// Error handling passes
#include "dgtl/pass/error_handling/Enhanced_Error_Recovery.hpp"

// Speculative passes
#include "dgtl/pass/speculative/Speculative_Alias_Hoister.hpp"
#include "dgtl/pass/speculative/Speculative_DCE.hpp"
#include "dgtl/pass/speculative/Speculative_Inliner.hpp"
#include "dgtl/pass/speculative/Temporal_Vector_Fusion.hpp"

// Tuning passes
#include "dgtl/pass/tuning/Auto_Tuner.hpp"
#include "dgtl/pass/tuning/Profile_Guided_Optimization.hpp"
#include "dgtl/pass/tuning/Microarchitecture_Tuning.hpp"

// Pass infrastructure
#include "dgtl/pass/orchestration/Pass_Composer.hpp"
#include "dgtl/pass/orchestration/Distributed_GPU_Scheduling.hpp"

// ============================================================================
// PIPELINE (execution modes and pass management)
// ============================================================================
#include "dgtl/pipeline/Analysis_Manager.hpp"
#include "dgtl/pipeline/Pass_Manager.hpp"
#include "dgtl/pipeline/Pass_Decision_Engine.hpp"
#include "dgtl/pipeline/Functional_Pipeline_Probes.hpp"
#include "dgtl/pipeline/Performance_Test_Probes.hpp"

// Pipeline modes (AOT/JIT/Hybrid)
#include "dgtl/pipeline/modes/AOT_Pipeline.hpp"
#include "dgtl/pipeline/modes/JIT_Pipeline.hpp"
#include "dgtl/pipeline/modes/Hybrid_Pipeline.hpp"

// ============================================================================
// ISA (instruction selection, lowering, register allocation, emission)
// ============================================================================
// Target definitions
#include "dgtl/isa/target/Wasm_Target.hpp"
#include "dgtl/isa/target/Wasm_Encoder.hpp"

// Register allocation
#include "dgtl/isa/regalloc/ARM64_RegAlloc.hpp"
#include "dgtl/isa/regalloc/X64_Reg_Map.hpp"

// ISA lowering
#include "dgtl/isa/lowering/Stack_Spiller.hpp"

// Code emission
#include "dgtl/isa/emitter/ARM64_Emitter.hpp"
#include "dgtl/isa/emitter/GPGPU_Fusion_FastEmission.hpp"
#include "dgtl/isa/emitter/MachOs_Emitter.hpp"
#include "dgtl/isa/emitter/NASM_Emitter.hpp"
#include "dgtl/isa/emitter/X64_Strike_Emitter.hpp"

// Cross-backend feature implementations (v1.0.2)
#include "dgtl/isa/emitter/ARM32_NEON_Gather_Scatter.hpp"
#include "dgtl/isa/emitter/Wasm_SIMD_Gather_Scatter_FMA.hpp"
#include "dgtl/isa/emitter/RiscV64_Gather_Scatter.hpp"

// Linking
#include "dgtl/isa/linker/Linker_Backend.hpp"

// ABI bridge
#include "dgtl/isa/bridge/ABI_Lowering.hpp"

// ============================================================================
// EXECUTION (binary loading, code caching, dispatching)
// ============================================================================
#include "dgtl/execution/cache/Code_Cache.hpp"
#include "dgtl/execution/dispatcher/Execution_Dispatcher.hpp"
#include "dgtl/execution/format/Binary_Format.hpp"
#include "dgtl/execution/format/Object_Format.hpp"
#include "dgtl/execution/loader/Binary_Loader.hpp"
#include "dgtl/execution/loader/Object_Loader.hpp"

// ============================================================================
// RUNTIME (task scheduling, context management)
// ============================================================================
#include "dgtl/runtime/TaskScheduler.hpp"
#include "dgtl/runtime/context/Runtime_Context.hpp"

// ============================================================================
// JIT (just-in-time compilation)
// ============================================================================
// Orchestration
#include "dgtl/jit/orchestrator/Binary_Orchestrator.hpp"
#include "dgtl/jit/orchestrator/MultiArch_Orchestrator.hpp"
#include "dgtl/jit/orchestrator/Strike_Orchestrator.hpp"
#include "dgtl/jit/orchestrator/Strike_Runner.hpp"

// Runtime components
#include "dgtl/jit/deopt/Deopt_Handler.hpp"
#include "dgtl/jit/runtime/Hardware_Scheduler.hpp"
#include "dgtl/jit/runtime/JIT_Executor.hpp"
#include "dgtl/jit/runtime/Tiered_Controller.hpp"

// Speculation
#include "dgtl/jit/speculation/Async_Speculator.hpp"

// Public API tier
#include "dgtl/api/Runtime.hpp"

