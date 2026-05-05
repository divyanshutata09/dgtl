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

# DGTL v1.0.0-proprietary-eval

DGTL is a high-performance compiler backend designed for teams that need low-latency, predictable code generation.

This release is focused on evaluation readiness for engineering leaders, platform teams, and performance-critical product groups.

## Why This Matters

- Faster path from high-level logic to optimized machine output
- Backend coverage across ARM64, x64, NASM, and WASM
- Deterministic integrity markers for reproducible validation
- Production-minded workflow with clear compliance controls

## What You Can Validate Today

- Multi-backend proof logs under benchmarks/logs/
- Shared integrity marker for baseline int-mode runs:
  - LogicHash: 0x9c13c402303ba602
- Observable lowering traceability in emitted text backends

## Included Proof Artifacts

- benchmarks/logs/release_arm64_int.log
- benchmarks/logs/release_x64_int.log
- benchmarks/logs/release_nasm_int.log
- benchmarks/logs/release_wasm_int.log

## Technical References

- docs/analysis/THEORY_OF_OPERATION.md
- docs/analysis/BACKEND_BRIDGE_PRE_REVEAL_PLAN.md
- docs/analysis/TECHNICAL_WHITEPAPER_OUTLINE.md
- benchmarks/README.md

## Quick Evaluation Run

PowerShell:

./build/dgtl_release.exe --backend arm64 --ir-mode int

## Licensing And Access

DGTL is provided under a proprietary software notice.

- License notice: LICENSE
- Evaluation and commercial contact: licensing@dgtl.tech
