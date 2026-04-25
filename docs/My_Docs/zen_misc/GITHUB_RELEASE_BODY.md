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

# DGTL v1.1.0-proprietary-eval

DGTL is now available under a proprietary software notice for evaluation and commercial licensing.

## What Is Included

- Multi-backend lowering evidence for arm64, x64, nasm, and wasm
- Deterministic logic integrity marker across baseline int-mode runs
- Public-facing theory and architecture documentation
- Validation flow with integrated proprietary header compliance checks

## Integrity And Backend Evidence

- LogicHash (int mode): 0x9c13c402303ba602
- ARM64 evidence: benchmarks/logs/release_arm64_int.log
- x64 evidence: benchmarks/logs/release_x64_int.log
- NASM evidence: benchmarks/logs/release_nasm_int.log
- WASM evidence: benchmarks/logs/release_wasm_int.log

## Technical Docs

- Theory of operation: docs/analysis/THEORY_OF_OPERATION.md
- Backend bridge plan: docs/analysis/BACKEND_BRIDGE_PRE_REVEAL_PLAN.md
- Technical whitepaper outline: docs/analysis/TECHNICAL_WHITEPAPER_OUTLINE.md
- Benchmark artifact guide: benchmarks/README.md

## Quick Run

PowerShell:

./build/dgtl_release.exe --backend arm64 --ir-mode int

## Licensing

- Notice: LICENSE
- Evaluation and commercial contact: licensing@dgtl.tech
