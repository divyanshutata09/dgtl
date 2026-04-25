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

# DGTL v1.2 Roadmap: Polyhedral Parallelization

Status: Draft v0.1  
Target Release Window: 2026-Q3

Execution Backlog: `docs/analysis/V1_2_POLYHEDRAL_EXECUTION_BACKLOG.md`

## 1) Objective

Ship DGTL v1.2 with production-grade polyhedral parallelization for affine loop nests, including:

- Legality-safe loop transformation (interchange, skew, tile, fuse/fission).
- Automatic parallelization across CPU cores with deterministic fallback.
- Improved locality (cache-aware tiling) and vector synergy.
- Clear profitability gating so transforms are applied only when beneficial.

## 2) Scope and Non-Goals

In scope for v1.2:

- Static-control loops (affine bounds, affine subscripts, analyzable dependences).
- Schedule-tree based transformations.
- OpenMP-style parallel loop emission in backends that support it.
- Runtime guard fallback for edge cases where needed.

Out of scope for v1.2:

- Full dynamic polyhedral compilation for non-affine kernels.
- GPU kernel generation.
- Distributed-memory scheduling.

## 3) Success Metrics (Release Gates)

Correctness:

- 0 known miscompiles in affine-poly test suite.
- Full existing regression suite remains green.

Performance:

- >= 1.30x median speedup on polyhedral benchmark set vs v1.1 baseline.
- >= 1.15x p95 speedup on memory-bound kernels after tiling.
- <= 3% compile-time overhead on non-poly workloads.

Safety and stability:

- Determinism preserved (identical IR hash and backend output for identical inputs/config).
- Rollback path validated for all guarded transforms.

## 4) Architecture Delta (v1.1 -> v1.2)

### 4.1 Analysis Layer

- Add affine SCoP detector and region extraction.
- Add dependence analysis for distance/direction vectors.
- Add legality checker for schedule transforms.

### 4.2 IR and Pass Layer

- Introduce schedule representation suitable for polyhedral transforms.
- Add passes:
  - Polyhedral region extractor
  - Dependence analyzer
  - Schedule optimizer
  - Loop tiler and parallelizer
  - Runtime guard insertion

### 4.3 Backend and Runtime Layer

- Lower parallel loops to backend-specific parallel constructs.
- Ensure compatibility with current register allocation and vector legalization.
- Add runtime fallback path where transformed assumptions fail.

### 4.4 Telemetry and Cost Model

- Extend AI cost model with polyhedral profitability features:
  - loop depth
  - reuse distance estimate
  - dependency density
  - tile footprint
- Record per-kernel transform decisions and speedup estimates.

## 5) Milestones

## M0: Foundation and Baseline (1-2 weeks)

Deliverables:

- Benchmark corpus and baseline measurements locked.
- Feature flags and config knobs introduced.
- Determinism snapshot for baseline builds.

Exit criteria:

- Baseline report committed.
- CI can run polyhedral-disabled and polyhedral-enabled pipelines.

## M1: Affine Region Detection (2-3 weeks)

Deliverables:

- SCoP detector for affine loop nests.
- Region annotation in IR/pipeline metadata.
- Negative classification tests for non-affine loops.

Exit criteria:

- >= 95% expected SCoP detection accuracy on internal corpus.
- No false-positive transform on non-affine tests.

## M2: Dependence and Legality Engine (2-3 weeks)

Deliverables:

- Dependence graph for affine accesses.
- Direction/distance vector computation.
- Legality checker for interchange, skew, tiling, fusion.

Exit criteria:

- Legality test suite green.
- Explicit rollback on uncertain legality.

## M3: Schedule Optimization and Tiling (3-4 weeks)

Deliverables:

- Schedule-tree optimizer.
- Locality-driven tiling heuristics.
- Fusion/fission with profitability and legality guards.

Exit criteria:

- Verified speedup on locality-sensitive benchmarks.
- No regressions in correctness suite.

## M4: Parallel Lowering and Runtime Guards (2-3 weeks)

Deliverables:

- Parallel loop lowering strategy per backend.
- Runtime guards and fallback blocks.
- Threading policy integration with existing scheduler.

Exit criteria:

- Correct parallel execution across supported platforms.
- Determinism preserved for fixed seed/config runs.

## M5: Hardening, Tuning, and Release Candidate (2 weeks)

Deliverables:

- Profitability tuning in AI cost model.
- End-to-end benchmarking and regression closure.
- Release notes and migration guidance.

Exit criteria:

- Success metrics in Section 3 met.
- v1.2 RC signed off.

## 6) Test Plan Expansion

New test categories:

- Functional:
  - affine loop interchange legality
  - skew + tile shape correctness
  - guarded fallback path execution
- Robustness:
  - malformed loop metadata
  - extreme trip counts and boundary conditions
  - parallel race-sensitive kernels with deterministic checking
- Performance:
  - stencil kernels
  - matmul-style blocked loops
  - memory stride stress workloads

Minimum CI matrix additions:

- polyhedral=off/on
- backend=arm64,x64,nasm,wasm
- test mode=sanity,full

## 7) Operational Rollout

Feature flags (proposed):

- --polyhedral-mode off|safe|aggressive
- --polyhedral-tile <size>
- --polyhedral-parallel auto|off|on

Rollout policy:

- Phase 1: default off, opt-in for benchmarking.
- Phase 2: default safe mode for selected benchmark profiles.
- Phase 3: default safe mode generally enabled in v1.2 if gates pass.

## 8) Risks and Mitigations

Risk: Miscompile due to dependence-analysis edge case.  
Mitigation: Conservative legality checks + mandatory fallback insertion + differential testing.

Risk: Compile-time blowup on complex nests.  
Mitigation: Analysis budget caps + early bailout to non-poly path.

Risk: Parallel overhead on short loops.  
Mitigation: Trip-count thresholding and runtime tuning in cost model.

Risk: Backend divergence in lowered parallel constructs.  
Mitigation: Common intermediate parallel form + backend conformance tests.

## 9) Tracking Template

Per-milestone reporting fields:

- planned start
- actual start
- planned end
- actual end
- completed deliverables
- open blockers
- perf delta vs baseline
- correctness status

## 10) Immediate Next Actions (This Week)

1. Freeze v1.1 benchmark baseline and create comparison dashboard.
2. Implement SCoP detector skeleton and metadata plumbing.
3. Add polyhedral feature flags to drivers and command surface.
4. Seed first functional tests for affine-detect and legality smoke checks.
