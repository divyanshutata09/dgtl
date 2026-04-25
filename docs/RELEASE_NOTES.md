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

# DGTL Release Notes

## Release

- Version: v1.1.0-proprietary-eval
- Date: 2026-04-16
- License model: Proprietary software notice (source-controlled evaluation/commercial access)

## Highlights

- Added a launch-ready, proprietary-facing repository posture.
- Published multi-backend proof-of-life artifacts for `arm64`, `x64`, `nasm`, and `wasm`.
- Added black-box technical documentation for public technical review.
- Enforced source header compliance in validation workflows.
- **v1.2**: Native object and library emission — ELF `.o`, GNU `.a`, COFF `.obj`, COFF `.lib`, Mach-O ARM64.
- **v1.2**: JIT execution via `--jit-execute` through `StrikeOrchestrator` with closed-loop AI cost feedback.
- **v1.2**: 9 backend targets in production driver (arm64, x64, nasm, wasm, elfobj, staticlib, coffobj, cofflib, macho).

## Backend Evidence (IRMode=int)

All backend logs report the same logic integrity marker:

- LogicHash: `0x9c13c402303ba602`

Per-backend evidence:

- ARM64 text lowering: `benchmarks/logs/release_arm64_int.log`
- x64 binary emission: `benchmarks/logs/release_x64_int.log`
- NASM text emission: `benchmarks/logs/release_nasm_int.log`
- WASM binary emission: `benchmarks/logs/release_wasm_int.log`

## Object & Library Emission Evidence

| Backend | CLI Name | Output | Magic / Signature | Status |
|---|---|---|---|---|
| ELF Relocatable | `elfobj` | `.o` | `7F 45 4C 46` (ELF64 ET_REL, 6 sections incl. `.rela.text`) | ✅ |
| GNU Archive | `staticlib` | `.a` | `!<arch>` (GNU `ar` format) | ✅ |
| COFF Object | `coffobj` | `.obj` | `64 86` (IMAGE_FILE_MACHINE_AMD64) | ✅ |
| COFF Library | `cofflib` | `.lib` | `!<arch>` + first linker member + COFF member | ✅ |
| Mach-O ARM64 | `macho` | `.macho` | `CF FA ED FE` (MH_MAGIC_64, LC_SEGMENT_64 + LC_SYMTAB) | ✅ |

## JIT Execution Evidence

- `--jit-execute` with `--backend x64` fires `StrikeOrchestrator::fire_strike()`
- Verified: `Strike OK status=0x0 executed_instructions=17 code_size=17`
- Requires `DGTL_STRIKE_EXECUTE=1` environment gate for W^X execution permission

## Notable Output Signals

- ARM64 output includes per-instruction trace markers (`TR_HASH`) for lowering traceability.
- NASM output includes node-level hash markers in emitted assembly comments.
- All captured proof logs include explicit polyhedral state (`[DGTL-POLY] Polyhedral analysis disabled`) under current defaults.

## Added Documentation

- Theory of operation and architecture flow: `docs/analysis/THEORY_OF_OPERATION.md`
- Backend bridge plan before public reveal: `docs/analysis/BACKEND_BRIDGE_PRE_REVEAL_PLAN.md`
- Whitepaper structure for proprietary publication: `docs/analysis/TECHNICAL_WHITEPAPER_OUTLINE.md`
- Deep technical response policy: `docs/business/DEEP_TECHNICAL_RESPONSE_POLICY.md`
- Benchmark artifact guide: `benchmarks/README.md`

## Compliance And Validation

- Proprietary header checker: `tools/scripts/verify_proprietary_headers.ps1`
- Checker supports summary mode, include/exclude control, and JSON output for CI.
- Header compliance gate is integrated into key validation commands (`run-all`, `run-strict-conformance`, `update-goldens`, `test`, `clean-build-and-run`, `header-check`).

## Distribution Notes

- Build binaries remain ignored in source control.
- Recommended distribution path for evaluators: GitHub Releases binaries + repository documentation + benchmark logs.

## Evaluation Contact

For commercial licensing or evaluation access:

- licensing@dgtl.tech
