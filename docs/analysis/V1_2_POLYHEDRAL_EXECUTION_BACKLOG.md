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

# DGTL v1.2 Polyhedral Parallelization: Execution Backlog

Status: Draft v0.1  
Companion Plan: `docs/analysis/V1_2_POLYHEDRAL_PARALLELIZATION_ROADMAP.md`

## 1) Prioritization Model

- P0: Blocking for feature completeness or safety.
- P1: High-value quality and performance work needed before RC.
- P2: Nice-to-have improvements after core gates pass.

## 2) Ownership and Tracking Fields

For each backlog item track:

- owner
- status: todo | in-progress | blocked | done
- target milestone: M0..M5
- ETA
- risk level: low | medium | high
- validation evidence (tests, benchmark report, conformance log)

## 3) Backlog (Mapped to Modules and Files)

## P0-001: Polyhedral CLI and Driver Flags

Goal:

- Introduce controllable feature flags for polyhedral mode and tiling/parallel policy.

Primary files (existing):

- `src/main.cpp`
- `src/apps/smoke_main.cpp`
- `dgtl.ps1`

Planned outputs:

- `--polyhedral-mode off|safe|aggressive`
- `--polyhedral-tile <size>`
- `--polyhedral-parallel auto|off|on`

Acceptance criteria:

- Flags parsed and reflected in runtime config.
- Default behavior matches v1.1 when polyhedral mode is off.

Milestone: M0

## P0-002: SCoP Detection Skeleton and Region Annotation

Goal:

- Detect affine candidate regions and annotate metadata for later passes.

Primary files (existing):

- `src/graph/Region.cpp`
- `src/graph/Execution_Graph.cpp`
- `src/ir/IR_Stream_Core.cpp`
- `include/dgtl/pass/transform/Poly_Transform.hpp`

Likely new files:

- `include/dgtl/analysis/polyhedral/Scop_Detector.hpp`
- `src/analysis/polyhedral/Scop_Detector.cpp`

Acceptance criteria:

- Regions are tagged as affine/non-affine.
- Non-affine regions are never transformed by polyhedral passes.

Milestone: M1

## P0-003: Dependence Graph and Legality Checker

Goal:

- Build dependence relations and enforce legality for interchange/skew/tile/fuse.

Primary files (existing):

- `include/dgtl/analysis/dataflow/Memory_Dependency_Smasher.hpp`
- `src/ir/IR_Stream_Operations.cpp`
- `src/ir/IR_Stream_Optimizations.cpp`

Likely new files:

- `include/dgtl/analysis/polyhedral/Dependence_Graph.hpp`
- `include/dgtl/analysis/polyhedral/Legality_Checker.hpp`
- `src/analysis/polyhedral/Dependence_Graph.cpp`
- `src/analysis/polyhedral/Legality_Checker.cpp`

Acceptance criteria:

- Direction/distance vectors produced for affine accesses.
- Unsafe transforms are rejected with explicit reason codes.

Milestone: M2

## P0-004: Transactional Safe Transform Commit and Rollback

Goal:

- Ensure polyhedral transforms preserve transactional semantics and can rollback on guard failure.

Primary files (existing):

- `src/ir/IR_Stream_Core.cpp`
- `src/ir/IR_Stream_Optimizations.cpp`
- `include/dgtl/pass/transform/Poly_Transform.hpp`

Acceptance criteria:

- Shadow integrity remains valid post-transform.
- Rollback path fully restores pre-transform IR state.

Milestone: M2/M4

## P0-005: Schedule Optimization and Tiling Core

Goal:

- Implement schedule-tree transforms and cache-aware tiling policy.

Primary files (existing):

- `include/dgtl/pass/transform/Poly_Transform.hpp`
- `src/ir/IR_Stream_Optimizations.cpp`

Likely new files:

- `include/dgtl/pass/transform/Poly_Scheduler.hpp`
- `include/dgtl/pass/transform/Poly_Tiler.hpp`
- `src/pass/transform/Poly_Scheduler.cpp`
- `src/pass/transform/Poly_Tiler.cpp`

Acceptance criteria:

- Interchange/skew/tile/fusion can be emitted when legal.
- Tile choices bounded by configurable heuristic budget.

Milestone: M3

## P0-006: Parallel Loop Lowering Path

Goal:

- Lower parallelizable loops into backend-compatible parallel constructs with deterministic behavior.

Primary files (existing):

- `src/apps/smoke_main.cpp`
- `src/main.cpp`
- `src//orchestrator/MultiArch_Orchestrator.cpp`
- `src//orchestrator/Strike_Runner.cpp`

Likely new files:

- `include/dgtl/pass/transform/Poly_Parallel_Lowering.hpp`
- `src/pass/transform/Poly_Parallel_Lowering.cpp`

Acceptance criteria:

- Parallel lowering guarded by profitability and policy flags.
- Deterministic mode available and validated.

Milestone: M4

## P1-007: Profitability Model Extension (Poly Features)

Goal:

- Add polyhedral-specific features to cost model and integrate decision logging.

Primary files (existing):

- `include/dgtl/analysis/cost/AI_Cost_Model.hpp`
- `src/apps/smoke_main.cpp`

Likely new files:

- `include/dgtl/analysis/cost/Polyhedral_Cost_Features.hpp`

Acceptance criteria:

- Decision path records estimated gain and selected transform set.
- Regressions minimized through threshold tuning.

Milestone: M5

## P1-008: Benchmark Harness and KPI Dashboard Data

Goal:

- Produce repeatable benchmark outputs aligned to roadmap release gates.

Primary files (existing):

- `src/apps/smoke_main.cpp`
- `docs/VERIFICATION_GUIDE.md`

Likely new files:

- `tests/performance/polyhedral/test_poly_stencil.cpp`
- `tests/performance/polyhedral/test_poly_matmul_tiled.cpp`
- `tests/performance/polyhedral/test_poly_stride_pressure.cpp`
- `docs/analysis/V1_2_POLYHEDRAL_BASELINE_REPORT.md`

Acceptance criteria:

- Baseline and post-transform metrics captured for all target backends.
- Median and p95 speedup targets traceable.

Milestone: M0/M5

## P1-009: Polyhedral Functional and Robustness Test Matrix

Goal:

- Expand correctness/robustness coverage for legality, guards, and edge cases.

Primary files (existing):

- `tests/test_suite_main.cpp`

Likely new files:

- `tests/functional/pipeline/test_poly_affine_detection.cpp`
- `tests/functional/pipeline/test_poly_legality_interchange.cpp`
- `tests/functional/pipeline/test_poly_tiling_correctness.cpp`
- `tests/robustness/core/test_poly_guard_rollback.cpp`

Acceptance criteria:

- New tests run under sanity/full policies as intended.
- No false-green behavior on failure paths.

Milestone: M1..M4

## P1-010: Command Runner Integration

Goal:

- Add high-level command workflows for polyhedral benchmarking and verification.

Primary files (existing):

- `dgtl.ps1`
- `README.md`

Acceptance criteria:

- Commands documented and discoverable in help output.
- Full suite can run with polyhedral on/off toggles.

Milestone: M0/M5

## P2-011: Advanced Fusion/Fission Heuristics

Goal:

- Improve transform quality for mixed locality and parallelism scenarios.

Primary files (existing):

- `include/dgtl/pass/transform/Poly_Transform.hpp`
- `src/ir/IR_Stream_Optimizations.cpp`

Acceptance criteria:

- Performance wins on mixed kernels without correctness regressions.

Milestone: Post-M5 or stretch in M5

## P2-012: Speculation Synergy for Runtime Specialization

Goal:

- Integrate polyhedral decisions with speculative runtime pathways.

Primary files (existing):

- `src//speculation/Async_Speculator.cpp`
- `include/dgtl/analysis/profiling/Speculative_Value_Profiler.hpp`

Acceptance criteria:

- Guarded specialization remains deterministic under strict mode.

Milestone: Post-M5

## 4) Suggested Sprint Sequencing

Sprint 1 (M0):

- P0-001, P1-008 (baseline half), P1-010 (flag plumbing)

Sprint 2 (M1):

- P0-002, P1-009 (affine detection tests)

Sprint 3 (M2):

- P0-003, P0-004

Sprint 4 (M3):

- P0-005, P1-009 (tiling and legality extensions)

Sprint 5 (M4):

- P0-006, P1-009 (guard rollback and parallel correctness)

Sprint 6 (M5):

- P1-007, P1-008 (final), P1-010 (docs + command polish)

## 5) Definition of Done (DoD)

An item is Done only if:

- Code merged and formatted to repository conventions.
- Related functional/robustness/performance tests added or updated.
- No regression in `dgtl test` and `dgtl test full`.
- Documentation updated in roadmap/backlog and README links.
