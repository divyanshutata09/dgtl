# DGTL Opcode/ISEL Sync Tracker

Last updated: 2026-05-21
Purpose: Track synchronization work for .dgtl_def and .dgtl_isel adoption across backend, , and analysis surfaces.

## Sync Basis
- IR opcode source: include/dgtl/ir/gen/defs/phase1_core_ops.dgtl_def
- ISel source: include/dgtl/isa/defs/x64_core.dgtl_isel
- Runtime contract: include/dgtl/isa/lowering/Backend_Runtime_Contract.hpp

## Status Legend
- DONE: updated and validated in current pass
- IN_PROGRESS: partially updated, follow-up required
- PENDING: not updated yet in this tracking cycle
- N/A: path alias or compatibility shim

## Explicit Paths Requested
- DONE: include/dgtl/backend/driver/Backend_Driver_API.hpp
- DONE: include/dgtl/backend/driver/Backend_Driver_Shared_Utils.hpp
- DONE: include/dgtl/backend/linker/Linker_Backend.hpp
- DONE: include/dgtl/backend/lowering/ABI_Lowering.hpp
- DONE: include/dgtl/backend/lowering/Stack_Spiller.hpp
- DONE: include/dgtl/isa/target/Wasm_Encoder.hpp
- DONE: include/dgtl/isa/target/Wasm_Target.hpp
- DONE: include/dgtl/jit/Async_Dopt_Tiered.hpp
- DONE: include/dgtl//Hardware_Scheduler.hpp
- DONE: include/dgtl/jit/JIT_Executor.hpp
- DONE: include/dgtl/jit/JIT_Orchestrator.hpp
- DONE: include/dgtl/pass/** (updated pass/transform/Transform_Scalar_Core.hpp for SELECT const-fold coverage)
- DONE: include/dgtl/execution/** (no opcode switch hotspots in current include tree scan)
- DONE: include/dgtl/analysis/** (no opcode switch hotspots in current include tree scan)
- DONE: include/dgtl/api/** (updated api/IRBuilder.hpp with SELECT/GEP/PTR_TO_INT/INT_TO_PTR/runtime-intrinsic helpers)
- DONE: include/dgtl/formal/** (updated formal/Formal_Verification_SMT.hpp with i64/SELECT SMT mappings)
- DONE: include/dgtl/graph/** (no opcode switch hotspots in current include tree scan)
- DONE: include/dgtl/core/** (updated core/Verified_Formal.hpp with SELECT solver/simplification)
- DONE: include/dgtl_sdk/** (updated dgtl_core.h opcode bridge enum for latest IR coverage)
- DONE: src/sdk/** (updated dgtl_sdk_impl.cpp bidirectional opcode mappings)
- DONE: include/dgtl/isa/lowering/Legalizer.hpp (added BRANCH_LIKELY/BRANCH_UNLIKELY canonicalization to BRANCH_COND)

## Current Cycle Changes
1. Updated Linker_Backend.hpp to include branch-likely/branch-unlikely sizing and emission support in x64 path.
2. Updated ABI lowering for WebAssembly to assign deterministic pseudo-register argument lanes.
3. Updated Wasm target text backend to:
   - honor BRANCH_LIKELY and BRANCH_UNLIKELY
   - route runtime-backed opcodes through runtime contract symbol calls
4. Updated hardware scheduler memory-port accounting to include:
   - GATHER_16
   - PREFETCH / PREFETCH_T0
   - CLFLUSH
5. Updated core ABI runtime concurrency classifier to semantic matching for evolving __dgtl_rt_* names.
6. Updated Wasm_Encoder to handle SELECT and BRANCH_LIKELY/BRANCH_UNLIKELY in structured fallback path.
7. Updated SDK opcode bridge surface:
   - include/dgtl_sdk/dgtl_core.h extended opcode enum set
   - src/sdk/dgtl_sdk_impl.cpp mapping sync for new enum and internal opcodes
8. Updated legalization layer:
   - include/dgtl/isa/lowering/Legalizer.hpp now canonicalizes BRANCH_LIKELY and BRANCH_UNLIKELY to BRANCH_COND while preserving operands/flags
9. Added targeted runtime-backed legalization policy layer:
   - include/dgtl/isa/lowering/Runtime_Legalization_Policy.hpp centralizes fallback policy
   - include/dgtl/isa/lowering/Instruction_Selector.hpp routes unmapped runtime-backed opcodes to Call form via policy
   - include/dgtl/isa/lowering/Legalizer.hpp emits explicit policy remarks for such opcodes

## User-Directed Constraint
- Do not create lower-case alias header include/dgtl/backend/linker/linkerbackend.hpp.
- Canonical linker path is include/dgtl/backend/linker/Linker_Backend.hpp.

## Validation Log
- Generation: dgtl.ps1 gen completed after opcode/isel updates
- Build: dgtl.ps1 build-release completed successfully after latest patches

## Next Sweep Plan
1. Pass directory: audit opcode switches and legality predicates for new opcodes.
2. Execution directory: audit call adapters and runtime import/export assumptions.
3. Analysis directory: audit opcode classification tables and cost models.
4. API/formal/core/graph/sdk: audit public-facing enums/mappers and docs alignment. (DONE for current known hotspots)
5. Legalizer: ensure legalization table covers new runtime-backed and branch variants. (DONE for branch hints and targeted runtime-backed policy layer)

## Directory Scan Findings (Current Sweep)
- pass: Transform_Scalar_Core.hpp updated to fold SELECT when cond/arms are constants.
- formal: Formal_Verification_SMT.hpp updated with i64 scalar mappings and SELECT ite export.
- core: Verified_Formal.hpp updated with SELECT constant evaluation and trivial SELECT simplification.
- execution, analysis, graph: no include-level opcode switch hotspots found by current search pattern.
- api: IRBuilder expanded to expose latest select/pointer/runtime-backed emission helpers.
- sdk: C SDK bridge enums/mappings extended so new ops round-trip cleanly through C ABI.
