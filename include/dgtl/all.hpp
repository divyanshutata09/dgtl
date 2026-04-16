#pragma once

// Core (foundational)
#include "dgtl/core/Arena.hpp"
#include "dgtl/core/Context.hpp"
#include "dgtl/core/Debug_Metadata.hpp"
#include "dgtl/core/Memory_Guard.hpp"
#include "dgtl/core/Symbol_Table.hpp"
#include "dgtl/core/Target.hpp"
#include "dgtl/core/Verified_Formal.hpp"

// IR (low-level to umbrella)
#include "dgtl/ir/base/IR_Types.hpp"
#include "dgtl/ir/base/IR_Nodes.hpp"
#include "dgtl/ir/base/IR_Traits.hpp"
#include "dgtl/ir/dialect/IR_Dialect_Interface.hpp"
#include "dgtl/ir/base/IR_Storage.hpp"
#include "dgtl/ir/base/IR_Stream.hpp"
#include "dgtl/ir/evolution/IR_Evolver.hpp"
#include "dgtl/ir/evolution/Mutation_Strategies.hpp"
#include "dgtl/ir/runtime.hpp/TLS_Flush_Merge.hpp"
#include "dgtl/ir/base/Global_IR.hpp"

// Analysis
#include "dgtl/analysis/cost/AI_Cost_Model.hpp"
#include "dgtl/analysis/dataflow/Liveness_Analyzer.hpp"
#include "dgtl/analysis/dataflow/Memory_Dependency_Smasher.hpp"
#include "dgtl/analysis/profiling/Speculative_Value_Profiler.hpp"

// Graph
#include "dgtl/graph/Dependency_Edge.hpp"
#include "dgtl/graph/Execution_Graph.hpp"
#include "dgtl/graph/Fusion_Cluster.hpp"
#include "dgtl/graph/Region.hpp"

// Pass (core transforms first)
#include "dgtl/pass/transform/DCE_Solver.hpp"
#include "dgtl/pass/transform/GVN.hpp"
#include "dgtl/pass/transform/Instruction_Folder.hpp"
#include "dgtl/pass/transform/Instruction_Fusion_Synth.hpp"
#include "dgtl/pass/transform/Masked_Predication_Lowering.hpp"
#include "dgtl/pass/transform/Instruction_Reordering.hpp"
#include "dgtl/pass/transform/Loop_Unswitcher.hpp"
#include "dgtl/pass/transform/Poly_Transform.hpp"
#include "dgtl/pass/simd/SIMD_Legalizer.hpp"
#include "dgtl/pass/simd/SIMD_Gather_Scatter_Optimizer.hpp"
#include "dgtl/pass/memory/Redundant_Store_Fuser.hpp"
#include "dgtl/pass/speculative/Speculative_DCE.hpp"
#include "dgtl/pass/speculative/Speculative_Inliner.hpp"
#include "dgtl/pass/speculative/Temporal_Vector_Fusion.hpp"
#include "dgtl/pass/speculative/Speculative_Alias_Hoister.hpp"
#include "dgtl/pass/tuning/Auto_Tuner.hpp"

// Pipeline
#include "dgtl/pipeline/Analysis_Manager.hpp"
#include "dgtl/pipeline/Functional_Pipeline_Probes.hpp"
#include "dgtl/pipeline/Pass_Decision_Engine.hpp"
#include "dgtl/pipeline/Pass_Manager.hpp"

// ISA (targets/regalloc/lowering before emitters)
#include "dgtl/isa/target/Wasm_Target.hpp"
#include "dgtl/isa/target/Wasm_Encoder.hpp"
#include "dgtl/isa/regalloc/ARM64_RegAlloc.hpp"
#include "dgtl/isa/regalloc/X64_Reg_Map.hpp"
#include "dgtl/isa/lowering/Stack_Spiller.hpp"
#include "dgtl/isa/emitter/ARM64_Emitter.hpp"
#include "dgtl/isa/emitter/GPGPU_Fusion_FastEmission.hpp"
#include "dgtl/isa/emitter/MachOs_Emitter.hpp"
#include "dgtl/isa/emitter/NASM_Emitter.hpp"
#include "dgtl/isa/emitter/X64_Strike_Emitter.hpp"
#include "dgtl/isa/linker/Linker_Backend.hpp"

// JIT
#include "dgtl/jit/orchestrator/Binary_Orchestrator.hpp"
#include "dgtl/jit/orchestrator/MultiArch_Orchestrator.hpp"
#include "dgtl/jit/orchestrator/Strike_Orchestrator.hpp"
#include "dgtl/jit/orchestrator/Strike_Runner.hpp"
#include "dgtl/jit/runtime/Hardware_Scheduler.hpp"
#include "dgtl/jit/runtime/JIT_Executor.hpp"
#include "dgtl/jit/runtime/Tiered_Controller.hpp"
#include "dgtl/jit/speculation/Async_Speculator.hpp"