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

#include <cstdint>
#include <cctype>
#include <chrono>
#include <cmath>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <cstdlib>
#include <vector>

// ── Core / IR foundations ────────────────────────────────────────────────────
#include "dgtl/core/Arena.hpp"
#include "dgtl/semantic/Symbol_Table.hpp"
#include "dgtl/core/Target.hpp"
#include "dgtl/core/Verified_Formal.hpp"
#include "dgtl/ir/base/IR_Node.hpp"
#include "dgtl/ir/base/IR_Stream.hpp"
// ── IR evolution ─────────────────────────────────────────────────────────────
#include "dgtl/ir/evolver/IR_Evolver_Mutation.hpp"
// ── Analysis ─────────────────────────────────────────────────────────────────
#include "dgtl/analysis/AI_Cost_Model.hpp"
#include "dgtl/analysis/Liveness_Memory_Dependency_Unified.hpp"
// ── Optimization passes ───────────────────────────────────────────────────────
#include "dgtl/pass/transform/Transform_Scalar_Core.hpp"
#include "dgtl/pass/transform/Transform_Control_Predication.hpp"
#include "dgtl/pass/speculative/Speculate_Alias_DCE_Inliner_Temporal_Vector_Fusion.hpp"
#include "dgtl/pass/simd/SimdGatherScatter_Legal_Vector_Recognizer.hpp"
// ── Pipeline probes ───────────────────────────────────────────────────────────
#include "dgtl/pipeline/Pipeline_Probes.hpp"
// ── ISA register allocation ───────────────────────────────────────────────────
#include "dgtl/isa/regalloc/ARM64_RegAlloc.hpp"
// ── ISA emitters ──────────────────────────────────────────────────────────────
#include "dgtl/isa/emitter/ARM64_Emitter.hpp"
#include "dgtl/isa/lowering/X64_Machine_Form_Lowering.hpp"
#include "dgtl/ir/gen/DGTL_Gen_Ops.hpp"
#include "dgtl/isa/emitter/NASM_Emitter.hpp"
#include "dgtl/isa/emitter/Arm32neon_RiscV64_WasmGSFMA.hpp"
#include "dgtl/isa/emitter/MachO_Emitter.hpp"
#include "dgtl/isa/target/Wasm_Encoder.hpp"
#include "dgtl/backend/linker/Linker_Backend.hpp"
#include "dgtl/backend/driver/Backend_Driver_API.hpp"
#include "dgtl/backend/driver/Backend_Driver_Shared_Utils.hpp"
// ── JIT / pipeline orchestration ─────────────────────────────────────────────
#include "dgtl/jit/Hardware_Scheduler.hpp"
#include "dgtl/jit/JIT_Orchestrator.hpp"
#include "dgtl/pipeline/Pass_Manager.hpp"

#include <thread>

namespace {

#include "smoke_cli_and_options.inl"
#include "smoke_ir_builders_and_profiles.inl"
#include "smoke_emitters_and_io.inl"
#include "smoke_release_checks.inl"
#include "smoke_benchmark_pipeline.inl"

} // namespace

#include "smoke_main_entry.inl"