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

# DGTL: High-Performance Compiler Infrastructure

**DGTL** is a retargetable compiler infrastructure designed for high-performance code generation and optimization. It targets **AI inference, high-frequency trading (HFT), cloud computing, and embedded systems** requiring low-latency, vectorized code generation.

## Access And Licensing

DGTL is released under a proprietary software notice. Source access, usage rights, and distribution rights are controlled by written agreement.

- Repository notice: [LICENSE](LICENSE)
- Licensing/evaluation draft terms: [PROPRIETARY_LICENSE_AND_EVALUATION_DRAFT.md](docs/business/PROPRIETARY_LICENSE_AND_EVALUATION_DRAFT.md)
- Technical disclosure policy for deep questions: [DEEP_TECHNICAL_RESPONSE_POLICY.md](docs/business/DEEP_TECHNICAL_RESPONSE_POLICY.md)
- Technical whitepaper outline (black-box publication model): [TECHNICAL_WHITEPAPER_OUTLINE.md](docs/analysis/TECHNICAL_WHITEPAPER_OUTLINE.md)

For commercial licensing or evaluation access, contact: **licensing@dgtl.tech**

## Core Features

- **Bit-Packed Intermediate Representation (BIR 2.1)**: Compact, SIMD-friendly IR with transactional semantics
- **Transactional Memory Model**: Thread-local arenas with speculative execution and rollback via shadow buffers
- **AI-Guided Optimization**: Neural network-based cost prediction for intelligent pass ordering
- **Multi-Target Code Generation**: ARM64/AArch64, x86-64, WebAssembly, NASM assembly
- **Native Object & Library Emission**:
  - ELF64 relocatable objects (`.o`) with `.rela.text` relocations
  - GNU `ar` static archives (`.a`)
  - COFF relocatable objects (`.obj`, IMAGE_FILE_MACHINE_AMD64)
  - COFF import libraries (`.lib`) with first linker member
  - Mach-O 64-bit ARM64 objects with real instruction encoding
- **Advanced Optimization Passes**: 
  - Global Value Numbering (GVN) with SIMD fingerprinting
  - Dead Code Elimination, Constant Folding
  - SIMD legalization, vectorization, and gather/scatter optimization
  - Loop unswitching, polyhedral transformations, speculative inlining
  - Memory dependency analysis and redundant store fusion
- **JIT Execution Runtime**: W^X-protected code execution via `StrikeOrchestrator` with closed-loop AI cost feedback
- **Graph-Based Analysis**: Execution graphs, dependency tracking, region-level optimization

## Performance-First Lowering

DGTL lowers high-level logic to backend-native output with traceability markers for post-run analysis.

```armasm
// Example Output: Arithmetic Lowering (ARM64, IRMode=int)
_DGTL_entry:
  add x20, x10, x11               // TR_HASH:0x98786E76E5341832
  add x21, x12, x20               // TR_HASH:0x98786E76E511C260
  add x22, x13, x20               // TR_HASH:0x98786E76E51C4600
  mul x23, x21, x22               // TR_HASH:0x98786E76ED599010
  ret
```

Current proof point from production run:

- `LogicHash=0x9c13c402303ba602`
- per-instruction `TR_HASH` tags in emitted assembly
- artifact logs under `benchmarks/logs/`

## Repository Face For Proprietary Launch

- Source is organized under `src/` and public interfaces under `include/dgtl/`.
- Build artifacts (including `dgtl_release.exe`) are ignored via `.gitignore`.
- Binary distribution should go through GitHub Releases, not through committed build outputs.
- Public technical depth is documented under `docs/analysis/` with black-box disclosure boundaries.

## Backend Bridge Before Public Reveal

ARM64 lowering is stable; backend parity is now established across all 9 targets including native object/library emission and JIT execution.

Recommended pre-reveal bridge gate:

1. Emit and verify `int` profile parity across `arm64`, `x64`, `nasm`, `wasm`.
2. Verify object/library output: `elfobj`, `staticlib`, `coffobj`, `cofflib`, `macho`.
3. Validate `--jit-execute` with `DGTL_STRIKE_EXECUTE=1` on x64.
4. Record release logs and logic hashes per backend in `benchmarks/logs/`.
5. Publish backend bridge status and risks in `docs/analysis/BACKEND_BRIDGE_PRE_REVEAL_PLAN.md`.
6. Keep polyhedral mode documented as staged (`off|safe|aggressive`) with clear enablement criteria.

## Public Reveal Kit

Use this section when publishing the first public-facing repository/release post.

### Backend Parity Snapshot (IRMode=int)

| Backend | Output Kind | Status | LogicHash | Evidence |
|---|---|---|---|---|
| `arm64` | text assembly | Ready | `0x9c13c402303ba602` | `benchmarks/logs/release_arm64_int.log` |
| `x64` | binary bytes | Ready | `0x9c13c402303ba602` | `benchmarks/logs/release_x64_int.log` |
| `nasm` | text assembly | Ready | `0x9c13c402303ba602` | `benchmarks/logs/release_nasm_int.log` |
| `wasm` | binary bytes | Ready | `0x9c13c402303ba602` | `benchmarks/logs/release_wasm_int.log` |
| `elfobj` | ELF .o binary | Ready | — | Verified: ELF64 ET_REL, 6 sections |
| `staticlib` | GNU .a archive | Ready | — | Verified: `!<arch>` header |
| `coffobj` | COFF .obj binary | Ready | — | Verified: `64 86` (MACHINE_AMD64) |
| `cofflib` | COFF .lib archive | Ready | — | Verified: first linker member + COFF member |
| `macho` | Mach-O ARM64 binary | Ready | — | Verified: `CF FA ED FE`, LC_SEGMENT_64 + LC_SYMTAB |

### Copy-Paste Reveal Announcement

```markdown
DGTL is now available under a proprietary software notice for evaluation and commercial licensing.

What is included in this reveal:
- Multi-target lowering evidence across ARM64, x64, NASM, and WASM.
- Native object/library emission: ELF .o, GNU .a, COFF .obj, COFF .lib, Mach-O ARM64.
- JIT execution via `--jit-execute` (x64, W^X-protected, closed-loop AI feedback).
- Deterministic `LogicHash` traceability (`0x9c13c402303ba602`) on baseline int-mode runs.
- Backend proof logs under `benchmarks/logs/`.
- Public theory documentation and architecture flow under `docs/analysis/THEORY_OF_OPERATION.md`.

Sample runs:
`./build/dgtl_release.exe --backend arm64 --ir-mode int`
`./build/dgtl_release.exe --backend coffobj --emit-output kernel.obj`
`./build/dgtl_release.exe --backend x64 --jit-execute`

Licensing and evaluation:
- Notice: `LICENSE`
- Contact: licensing@dgtl.tech
```

## Requirements

- **C++26** compatible compiler (GCC 15.2.0+ with UCRT64 on Windows, G++ on Linux/macOS)
- **Windows 10+**, **Linux (glibc 2.31+)**, or **macOS 11+**
- **PowerShell 5.1+** on Windows for the `dgtl` command runner

## Building

### One-Time Setup

Install the short `dgtl` command into your PowerShell profile:

```powershell
.\tools\scripts\install_dgtl_command.ps1
```

Open a new PowerShell session, then verify the command is available:

```powershell
dgtl help
```

### Build Commands

```powershell
dgtl build
```

Builds the production driver at `build\dgtl_release.exe`.

Run the production driver with explicit IR mode selection:

```powershell
.\build\dgtl_release.exe --backend arm64 --ir-mode int
.\build\dgtl_release.exe --backend arm64 --ir-mode float
.\build\dgtl_release.exe --backend arm64 --ir-mode float-simd-widen
.\build\dgtl_release.exe --backend arm64 --ir-mode fcvtl
.\build\dgtl_release.exe --backend arm64 --ir-mode float-simd-narrow
.\build\dgtl_release.exe --backend arm64 --ir-mode fcvtn
```

`--ir-mode int` is the default v1.1 release path. `--ir-mode float` enables the float profile and FMA-capable fusion path. `--ir-mode float-simd-widen` (alias `--ir-mode fcvtl`) emits an ARM64 SIMD widening probe using `fcvtl` (`vN.4h` -> `vD.4s`). `--ir-mode float-simd-narrow` (alias `--ir-mode fcvtn`) emits an ARM64 SIMD narrowing probe using `fcvtn` (`vN.4s` -> `vD.4h`).

Emit native object files and libraries:

```powershell
.\build\dgtl_release.exe --backend elfobj --emit-output kernel.o
.\build\dgtl_release.exe --backend staticlib --emit-output libkernel.a
.\build\dgtl_release.exe --backend coffobj --emit-output kernel.obj
.\build\dgtl_release.exe --backend cofflib --emit-output kernel.lib
.\build\dgtl_release.exe --backend macho --emit-output kernel.macho
```

JIT-compile and execute x64 machine code in-process (requires `DGTL_STRIKE_EXECUTE=1` env gate):

```powershell
$env:DGTL_STRIKE_EXECUTE="1"
.\build\dgtl_release.exe --backend x64 --jit-execute
```

`--jit-execute` invokes the full `StrikeOrchestrator` pipeline: emit → W^X inject → execute → closed-loop AI cost feedback. Only valid with `--backend x64`.

Polyhedral control flags are available in both drivers:

```powershell
--polyhedral-mode off|safe|aggressive
--polyhedral-tile <n>
--polyhedral-parallel auto|off|on
```

Defaults are `--polyhedral-mode off`, `--polyhedral-parallel auto`, and `--polyhedral-tile 0` (auto tile sizing).

```powershell
dgtl smoke-build
```

Builds the smoke and conformance driver at `build\dgtl_main.exe`.

```powershell
dgtl strict
```

Builds a strict variant at `build\dgtl_main_strict.exe` using warning-heavy flags and `-Werror`.

## Command Reference

All commands below are run from the repository root using the PowerShell `dgtl` function.

### Pick by Intent (Recommended)

Use the descriptive names below to reduce ambiguity:

```powershell
dgtl build-release
dgtl build-smoke
dgtl build-strict
dgtl run-all
dgtl run-backend x64
dgtl bench-backend x64 fusion
dgtl update-goldens
dgtl run-strict-conformance
dgtl test
dgtl test full
dgtl clean-all
dgtl clean-build-and-run
```

### Production Workflow

Use this sequence when building and validating a release for deployment:

| Step | Command | Purpose |
|---|---|---|
| 1 — Build | `dgtl build-release` | Compiles the production binary at `build\dgtl_release.exe`. Only this binary is a production build. |
| 2 — Validate | `dgtl run-all` | Full conformance across all 4 backends (arm64, x64, nasm, wasm). |
| 2a — Strict gate | `dgtl run-strict-conformance` | Strict executable gate — catches edge cases not covered by default conformance. |
| 2b — Aggregate tests | `dgtl test` | Functional + robustness sanity suite (fast, incremental). Use `dgtl test full` to include heavy stress tests. |
| 3 — Benchmark (optional) | `dgtl bench-backend x64` | Timing profile for your target backend before shipping. |
| All-in-one | `dgtl clean-build-and-run` | Clean → rebuild → full conformance in one command. |

> **Note:** `dgtl build-smoke` and `dgtl build-strict` produce **test/debug binaries only** — do not use them as production artifacts.

### Categories and When to Use

#### 1) Build Binaries

Use when: you need an executable before running validation or benchmarks.

```powershell
dgtl build-release
dgtl build-smoke
dgtl build-strict
```

Outputs:

- `dgtl build-release` -> `build\dgtl_release.exe`
- `dgtl build-smoke` -> `build\dgtl_main.exe`
- `dgtl build-strict` -> `build\dgtl_main_strict.exe`

#### 2) Validate Correctness

Use when: you want to confirm backend output and conformance behavior.

```powershell
dgtl run-all
dgtl run-backend arm64
dgtl run-backend x64
dgtl run-backend nasm
dgtl run-backend wasm
dgtl run-strict-conformance
dgtl test
dgtl test full
```

#### 3) Benchmark Performance

Use when: you want optimization/emission timing data for one backend.

```powershell
dgtl bench-backend <backend> [profile|iterations] [pressure-vars] [dump]
```

You can optionally append polyhedral controls to benchmark runs:

```powershell
dgtl bench-backend x64 fusion --polyhedral-mode safe --polyhedral-tile 64 --polyhedral-parallel on
```

Profiles: `fold` (default), `fusion`, `simd`, `pressure`, `predication`.

Examples:

```powershell
dgtl bench-backend x64
dgtl bench-backend x64 1000
dgtl bench-backend x64 fusion
dgtl bench-backend x64 simd
dgtl bench-backend x64 pressure 64
dgtl bench-backend arm64 predication
dgtl bench-backend x64 fusion dump
dgtl bench-backend wasm simd
dgtl bench-backend nasm fusion dump
```

For ARM64, benchmark output now includes AI telemetry-triggered hot-path retuning signals:

- `hot_swaps=<n>`: Number of iterations where adaptive mode switching occurred
- `hot_swap_mode=<mode>`: Last applied adaptive mode (for example `int->float-simd/predication`)

#### 4) Manage Golden Baselines

Use when: backend output changes are intentional and expected baselines must be refreshed.

```powershell
dgtl update-goldens
dgtl update-goldens strict
```

#### 5) Cleanup and One-Shot Flow

Use when: you want a clean workspace state or a clean-build-run sequence.

```powershell
dgtl clean-all
dgtl clean-build-and-run
```

### Legacy Short Names (Still Supported)

Short names remain available for existing scripts and muscle memory:

```powershell
dgtl help
dgtl build
dgtl smoke-build
dgtl strict
dgtl run
dgtl run arm64
dgtl bench x64 fusion
dgtl update-golden
dgtl srun
dgtl clean
dgtl crun
```

Alias mapping:

| Recommended Name | Legacy Short Name |
|---|---|
| `dgtl build-release` | `dgtl build` |
| `dgtl build-smoke` | `dgtl smoke-build` |
| `dgtl build-strict` | `dgtl strict` |
| `dgtl run-all` | `dgtl run` |
| `dgtl run-backend <backend>` | `dgtl run <backend>` |
| `dgtl bench-backend <backend> ...` | `dgtl bench <backend> ...` |
| `dgtl update-goldens [strict]` | `dgtl update-golden [strict]` |
| `dgtl run-strict-conformance` | `dgtl srun` |
| `dgtl test` | `dgtl test` |
| `dgtl test full` | `dgtl test full` |
| `dgtl clean-all` | `dgtl clean` |
| `dgtl clean-build-and-run` | `dgtl crun` |

### Conformance Prerequisite (Important)

Full conformance commands (`dgtl run-all`, `dgtl run-strict-conformance`, `dgtl update-goldens`, `dgtl update-goldens strict`, and `dgtl clean-build-and-run`) call a PowerShell conformance script expected at:

```text
My_Docs\commands\backend_conformance_check.ps1
or
docs\My_Docs\commands\backend_conformance_check.ps1
```

If that file is missing, those commands fail with:

```text
Missing conformance script: ...\docs\My_Docs\commands\backend_conformance_check.ps1
```

Current repository layout note:

- The checked-in script is under `docs\My_Docs\commands\backend_conformance_check.ps1`.
- `dgtl.ps1` now checks both locations automatically.

Quick workaround when you only need smoke output from one backend:

```powershell
dgtl run-backend arm64
dgtl run-backend x64
dgtl run-backend nasm
dgtl run-backend wasm
```

| Recommended Command | Legacy Equivalent | Requires existing build? | What it does |
|---|---|---|---|
| `dgtl help` | `dgtl help` | No | Prints command usage and examples. |
| `dgtl build-release` | `dgtl build` | No | Builds production `build\dgtl_release.exe`. |
| `dgtl build-smoke` | `dgtl smoke-build` | No | Builds smoke/conformance `build\dgtl_main.exe`. |
| `dgtl build-strict` | `dgtl strict` | No | Builds strict smoke `build\dgtl_main_strict.exe` with strict warnings and `-Werror`. |
| `dgtl run-all` | `dgtl run` | No (auto-builds if missing) | Runs full backend conformance through the conformance script (all backends). Requires `My_Docs\commands\backend_conformance_check.ps1` or `docs\My_Docs\commands\backend_conformance_check.ps1`. |
| `dgtl run-backend <backend>` | `dgtl run <backend>` | No (auto-builds if missing) | Runs a single backend directly (`arm64`, `x64`, `nasm`, `wasm`) without the conformance script. |
| `dgtl bench-backend <backend> [profile\|iterations] [pressure-vars] [dump]` | `dgtl bench <backend> [profile\|iterations] [pressure-vars] [dump]` | Auto-builds strict if missing | Runs strict benchmark mode for one backend with profile and optional dump output. |
| `dgtl update-goldens` | `dgtl update-golden` | Auto-builds default if missing | Refreshes default conformance goldens through the conformance script. Requires `My_Docs\commands\backend_conformance_check.ps1` or `docs\My_Docs\commands\backend_conformance_check.ps1`. |
| `dgtl update-goldens strict` | `dgtl update-golden strict` | Auto-builds strict if missing | Refreshes strict conformance goldens through the conformance script. Requires `My_Docs\commands\backend_conformance_check.ps1` or `docs\My_Docs\commands\backend_conformance_check.ps1`. |
| `dgtl run-strict-conformance` | `dgtl srun` | Auto-builds strict if missing | Runs full backend conformance with strict executable through the conformance script. Requires `My_Docs\commands\backend_conformance_check.ps1` or `docs\My_Docs\commands\backend_conformance_check.ps1`. |
| `dgtl test` | `dgtl test` | Builds test binary if sources changed | Runs functional + robustness sanity suite. Incremental: reuses existing binary when sources are unchanged (~0.04s warm). |
| `dgtl test full` | `dgtl test full` | Builds test binary if sources changed | Same as `dgtl test` plus heavy stress tests (`robustness.core2`, `robustness.core4`, `performance.mt_codegen`, `performance.opt_strike`). |
| `dgtl clean-all` | `dgtl clean` | No | Deletes DGTL build outputs, conformance logs, and conformance cache files. |
| `dgtl clean-build-and-run` | `dgtl crun` | No | Executes `clean -> smoke-build -> run` (full conformance path, so the conformance script is required). |

### Command Runner Flags

- **Default build**: `-std=c++26 -O2 -I ./include`
- **Strict build**: `-std=c++26 -O2 -I ./include -Wall -Wextra -Wpedantic -Werror`
- **Custom tuning**: If you need flags such as `-march=native` or `-flto`, add them in your own local build flow rather than assuming the `dgtl` wrapper uses them.

## Running Tests

### Option 1: Run Full Backend Validation

```powershell
dgtl run-all
```

Legacy equivalent: `dgtl run`

Runs the conformance gate across `arm64`, `x64`, `nasm`, and `wasm` using `build\dgtl_main.exe`, then compares normalized output against stored goldens.

If `build\dgtl_main.exe` is missing, `dgtl run` auto-builds it.

`dgtl run-all` (legacy: `dgtl run`) also requires `My_Docs\commands\backend_conformance_check.ps1` or `docs\My_Docs\commands\backend_conformance_check.ps1`.

### Option 2: Run One Backend Only

```powershell
dgtl run-backend arm64
```

Legacy equivalent: `dgtl run arm64`

Replace `arm64` with `x64`, `nasm`, or `wasm` to run a quick single-backend smoke check without invoking the full conformance gate.

If `build\dgtl_main.exe` is missing, `dgtl run-backend <backend>` (legacy: `dgtl run <backend>`) auto-builds it.

Single-backend `dgtl run-backend <backend>` does not require the conformance script.

### Benchmark One Backend

Runs the strict executable in benchmark mode and prints split `opt_us`, `emit_us`, `total_us`, `output_size`, `fallback_ops` (x64 unsupported-op fallback count), node progression (`nodes=built->post_profile->post_core_opt`), and `exec_us` (x64 fold only).

Supported backends: `arm64`, `x64`, `nasm`, `wasm`.

Benchmark command form:

```powershell
dgtl bench <backend> [profile|iterations] [pressure-vars] [dump]
```

Argument rules:

- If the second argument is numeric, it is treated as `iterations` and profile defaults to `fold`.
- If the second argument is a profile name (`fold`, `fusion`, `simd`, `pressure`), it is treated as `profile`.
- `pressure-vars` applies only when profile is `pressure`.
- `dump` is optional and prints the benchmark-emitted artifact (`hex` for binary backends, text for NASM).
- The short `dgtl bench` wrapper currently supports either `iterations` or `profile` in that second position, not both simultaneously.

**Default profile (fold) — 2-node constant-fold IR, measures baseline emit cost:**

```powershell
dgtl bench x64
dgtl bench wasm
```

**Override iteration count (backward-compatible):**

```powershell
dgtl bench x64 1000
```

**Named profiles — benchmark a specific optimization workload:**

| Profile | IR workload | Key measurement |
|---------|-------------|-----------------|
| `fold` | 2-node constant-fold | baseline opt + emit |
| `fusion` | 6-node mul+add → FMA synthesis | fusion pass cost |
| `simd` | 13-node SIMD legalization | legalizer throughput |
| `pressure` | N-var register pressure | spill threshold |

```powershell
dgtl bench x64 fusion
dgtl bench x64 simd
dgtl bench nasm fusion
dgtl bench wasm simd
```

**Pressure test — configure variable count (default: 25):**

```powershell
dgtl bench x64 pressure 64
dgtl bench x64 pressure 100
```

**Dump benchmark artifact (hex for binary backends, text for NASM):**

```powershell
dgtl bench x64 fusion dump
dgtl bench wasm simd dump
dgtl bench nasm fusion dump
```

Strict benchmark executable behavior:

- `dgtl bench ...` auto-builds `build\dgtl_main_strict.exe` if missing.
- x64 runtime execution timing (`exec_us`) is reported only for the `fold` profile.

### Refresh Goldens Intentionally

```powershell
dgtl update-golden
```

Use this only when backend output changes are intentional and you want to refresh stored conformance baselines.

If you are validating the strict build, refresh that baseline explicitly:

```powershell
dgtl update-golden strict
```

Auto-build behavior:

- `dgtl update-golden` auto-builds `build\dgtl_main.exe` if missing.
- `dgtl update-golden strict` auto-builds `build\dgtl_main_strict.exe` if missing.
- Both commands require `My_Docs\commands\backend_conformance_check.ps1` or `docs\My_Docs\commands\backend_conformance_check.ps1`.

### Combined Workflow

```powershell
dgtl clean
dgtl smoke-build
dgtl run
```

Or use the equivalent one-shot command:

```powershell
dgtl crun
```

## Who Uses Which Commands

### Maintainers (this repository)

Use the `dgtl` command runner from the repository root for build, conformance, benchmark, and baseline workflows.

Recommended day-to-day maintainer flow:

```powershell
dgtl strict
dgtl srun
dgtl bench x64 fusion
```

When output changes are intentional:

```powershell
dgtl update-golden
dgtl update-golden strict
```

For exact behavior and argument forms, use the Command Reference above.

### Backend Consumers (users integrating DGTL)

If you are building your own language frontend and using DGTL as the backend, your primary interface is:

- DGTL C++ APIs in `include/dgtl/` (IR construction, optimization, emission)
- An integration contract in your own compiler driver (for example: `my_backend --ir-plan plan.json --target arm64 --out out.s`)

The maintainer `dgtl ...` commands are not the end-user product CLI for external frontend projects.

See `docs/analysis/LANGUAGE_BACKEND_USAGE.md` for the integration pattern.

## Testing

DGTL currently ships with test suites under `tests/` covering:

### Functional Tests (`tests/functional/`)
- **Pipeline tests**: Identity merge, SSA, NOP absorption, fusion, dependency tracking
- **SIMD operations**: 512-bit vector operations, gather/scatter optimization
- **Memory operations**: Unaligned loads, transactional rollback, stress scenarios
- **Intent validation**: Hash-based collision detection and error recovery

### Latency Benchmarks (`tests/latency/`)
- Cache killer patterns
- Fractal torture tests (branch misprediction stress)
- Ghost loads (non-temporal patterns)
- Micro-benchmarks for latency measurement
- Register exhaustion tests

### Performance Tests (`tests/performance/`)
- Multi-threaded Arena performance
- SIMD micro-kernels
- Stress tests under high load

## Quick Start: Your First IR Emission

Create a minimal program that builds a tiny IR stream and emits ARM64 assembly:

```cpp
#include "dgtl/all.hpp"
#include <iostream>
#include <sstream>

int main() {
    dgtl::core::DGTL_Arena arena(1, 64);
    dgtl::core::SymbolTable symbols(arena);
    dgtl::ir::IRStream stream(arena, 16);

    stream.begin_transaction();
    stream.push_node(
        dgtl::ir::OpCode::LOAD_IMM,
        dgtl::ir::Flags::IS_CONST,
        1,
        dgtl::ir::INVALID_SLOT,
        84,
        dgtl::ir::INVALID_SLOT,
        0
    );
    stream.push_node(
        dgtl::ir::OpCode::RET,
        dgtl::ir::Flags::IS_RETURN,
        0,
        1,
        dgtl::ir::INVALID_SLOT,
        dgtl::ir::INVALID_SLOT,
        0
    );
    stream.commit_transaction();

    stream.perform_gvn();

    dgtl::isa::ARM64Emitter emitter(stream, symbols);
    dgtl::isa::ARM64Emitter::Config config;
    config.verbose_asm = true;

    std::ostringstream asm_out;
    emitter.emit_assembly(asm_out, config);
    std::cout << asm_out.str();
    return 0;
}
```

Compile and run:
```bash
g++ -std=c++26 -O2 -I./include your_program.cpp -o your_program.exe
./your_program.exe
```

For repository validation, prefer the built-in smoke/conformance flow instead of ad hoc compile commands:

```powershell
dgtl smoke-build
dgtl run arm64
```

## Architecture Overview

### Intermediate Representation (BIR 2.1)

DGTL's IR is organized in three tiers:

- **Tier 1 (Atom)**: OpCode + Flags (8 bytes, packed for density)
- **Tier 2 (Core)**: Result ID + 3 Operands + Intent Hash (32 bytes, cache-line aligned)
- **Tier 3 (Joined)**: Unified IRNode structure for emission

This design enables:
- Transactional mutations via shadow buffers
- SIMD-accelerated fingerprinting in GVN
- Zero-copy speculative execution with rollback
- Intent-hash validation to prevent hallucination

### Compilation Pipeline

```
IR Construction (IRStream) 
  ↓
Analysis (Liveness, Memory Deps, Cost Modeling)
  ↓
Transformations (GVN, DCE, SIMD Legalization, etc.)
  ↓
Polyhedral Analysis (SCoP detection, tiling, parallelization)
  ↓
Graph Scheduling (Region-level, multi-backend aware)
  ↓
ISA Emission (ARM64 / x86-64 / WASM / NASM)
  ↓                          ↓
Object/Library Emission      JIT Execution (--jit-execute)
(ELF .o / .a / COFF .obj    (StrikeOrchestrator: W^X inject
 / .lib / Mach-O)             + execute + AI cost feedback)
```

### Supported Backends

| Backend | CLI Name | Status | Use Case |
|---------|----------|--------|----------|
| **ARM64 (AArch64)** | `arm64` | ✅ Primary | Mobile, edge, AWS Graviton |
| **x86-64** | `x64` | ✅ Primary | Intel/AMD servers, HFT workstations |
| **WebAssembly** | `wasm` | ✅ Complete | Browser, cloud-native portability |
| **NASM** | `nasm` | ✅ Complete | Traditional assembly toolchains |
| **ELF Relocatable** | `elfobj` | ✅ Complete | Linux `.o` for `ld` / `gcc` linking |
| **Static Archive** | `staticlib` | ✅ Complete | GNU `ar` `.a` for static linking |
| **COFF Object** | `coffobj` | ✅ Complete | Windows `.obj` for MSVC `link.exe` |
| **COFF Library** | `cofflib` | ✅ Complete | Windows `.lib` import library |
| **Mach-O ARM64** | `macho` | ✅ Complete | macOS/iOS ARM64 object files |

## Key Components

### Core (`include/dgtl/core/`)
- `Arena.hpp`: Thread-local, transactional memory allocation with huge-page alignment
- `Context.hpp`: Compiler context with fast emit paths
- `Symbol_Table.hpp`: IR reference tracking
- `Memory_Guard.hpp`: Safety invariant enforcement
- `Target.hpp`: Backend target specification

### IR (`include/dgtl/ir/`)
- `IR_Types.hpp`: OpCode definitions and flags
- `IR_Stream.hpp`: Transactional IR streaming with shadow buffers
- `IR_Dialect_Interface.hpp`: Dialect rules (AI opcodes, HFT opcodes, etc.)
- `IR_Evolver.hpp`: IR mutation strategies for testing

### Analysis (`include/dgtl/analysis/`)
- `AI_Cost_Model.hpp`: Neural network-based cycle cost prediction
- `Liveness_Analyzer.hpp`: Live variable analysis
- `Memory_Dependency_Smasher.hpp`: Memory dependency elimination
- `Speculative_Value_Profiler.hpp`: Value profiling for speculation

### Optimization Passes (`include/dgtl/pass/`)
- **Transform**: GVN, DCE, constant folding, instruction reordering, fusion
- **SIMD**: Legalization, gather/scatter optimization, vectorization
- **Memory**: Redundant store fusion
- **Speculative**: Speculative DCE, inlining, alias hoisting
- **Tuning**: Auto-tuner for parameter selection

### ISA & Codegen (`include/dgtl/isa/`)
- `ARM64_Emitter.hpp` / `X64_Strike_Emitter.hpp`: Assembly generation
- `ARM64_RegAlloc.hpp` / `X64_Reg_Map.hpp`: Register allocation
- `Wasm_Encoder.hpp`: WebAssembly binary encoding
- `MachOs_Emitter.hpp`: Mach-O 64-bit ARM64 object emission with real instruction encoding
- `Linker_Backend.hpp`: ELF64 relocatable, GNU `ar` archive, COFF `.obj`, and COFF `.lib` emission
- `Stack_Spiller.hpp`: Spill code generation

### JIT Runtime (`include/dgtl/jit/`)
- `JIT_Executor.hpp`: W^X memory protection, cross-platform I-Cache sync
- `Strike_Orchestrator.hpp`: Full compile→inject→execute→feedback pipeline (`--jit-execute`)
- `Strike_Runner.hpp`: Low-level VirtualAlloc/mmap executable memory injection
- `MultiArch_Orchestrator.hpp`: Multi-target dispatch (ARM64, x64, NASM, WASM, ELF, COFF, Mach-O)
- `Hardware_Scheduler.hpp`: Hardware-aware execution scheduling
- `Async_Speculator.hpp`: Asynchronous speculation support

## Documentation

- [BitPacked_IR_Core.md](docs/analysis/DGPM_SPEC.md): IR architecture deep-dive
- [V1_2_POLYHEDRAL_PARALLELIZATION_ROADMAP.md](docs/analysis/V1_2_POLYHEDRAL_PARALLELIZATION_ROADMAP.md): v1.2 technical roadmap for polyhedral legality, scheduling, tiling, and parallel lowering
- [V1_2_POLYHEDRAL_EXECUTION_BACKLOG.md](docs/analysis/V1_2_POLYHEDRAL_EXECUTION_BACKLOG.md): prioritized implementation backlog mapped to concrete modules and files
- [THEORY_OF_OPERATION.md](docs/analysis/THEORY_OF_OPERATION.md): End-to-end backend flow and architecture diagram
- [BACKEND_BRIDGE_PRE_REVEAL_PLAN.md](docs/analysis/BACKEND_BRIDGE_PRE_REVEAL_PLAN.md): x86-64 and multi-backend parity checklist before public reveal
- [VERIFICATION_GUIDE.md](docs/VERIFICATION_GUIDE.md): Manual verification checklist
- [DGTL_BUSINESS_MODEL.md](docs/business/DGTL_BUSINESS_MODEL.md): Commercial packaging, target customers, and launch phases
- [DGTL_PRICING_AND_PILOT_OFFER.md](docs/business/DGTL_PRICING_AND_PILOT_OFFER.md): Concrete pricing structure and pilot offer
- [DGTL_ONE_PAGE_SALES_DECK.md](docs/business/DGTL_ONE_PAGE_SALES_DECK.md): One-page website / sales deck copy
- [DGTL_WEBSITE_LANDING_COPY.md](docs/business/DGTL_WEBSITE_LANDING_COPY.md): Website landing-page copy for commercial launch
- [DGTL_COMMERCIAL_PROPOSAL_TEMPLATE.md](docs/business/DGTL_COMMERCIAL_PROPOSAL_TEMPLATE.md): Reusable pilot and commercial proposal template
- [DGTL_BRANDED_PROPOSAL.md](docs/business/DGTL_BRANDED_PROPOSAL.md): Customer-facing polished proposal document
- [DGTL_HFT_PROPOSAL.md](docs/business/DGTL_HFT_PROPOSAL.md): HFT and low-latency specific proposal variant
- [DGTL_AI_RUNTIME_PROPOSAL.md](docs/business/DGTL_AI_RUNTIME_PROPOSAL.md): AI runtime and execution team proposal variant
- [DGTL_DSL_ENTERPRISE_LANGUAGE_PROPOSAL.md](docs/business/DGTL_DSL_ENTERPRISE_LANGUAGE_PROPOSAL.md): Internal DSL and enterprise language proposal variant
- [PROPRIETARY_LICENSE_AND_EVALUATION_DRAFT.md](docs/business/PROPRIETARY_LICENSE_AND_EVALUATION_DRAFT.md): Draft proprietary licensing and evaluation terms
- [DEEP_TECHNICAL_RESPONSE_POLICY.md](docs/business/DEEP_TECHNICAL_RESPONSE_POLICY.md): How to answer deep technical questions while protecting implementation IP
- [PRODUCT_POSITIONING.md](docs/business/PRODUCT_POSITIONING.md): Customer-facing positioning and messaging
- [TECHNICAL_WHITEPAPER_OUTLINE.md](docs/analysis/TECHNICAL_WHITEPAPER_OUTLINE.md): Black-box whitepaper structure, benchmark evidence plan, and assembly demo format
- [LICENSE](LICENSE): Release-facing proprietary software notice
- [RELEASE_NOTES.md](docs/RELEASE_NOTES.md): Release summary, backend evidence, and distribution notes
- [DEPENDENCY_VERSION_CONTROL.md](docs/DEPENDENCY_VERSION_CONTROL.md): Locked tool versions
- [benchmarks/README.md](benchmarks/README.md): Proof-of-life benchmark logs and regeneration instructions
- [docs/My_Docs/](docs/My_Docs/): Detailed module documentation and examples

## Project Structure

```
DGTL/
├── include/dgtl/           # Public header files (all compilation context)
│   ├── core/               # Arena, Context, Symbol Table, Memory Guards
│   ├── ir/                 # IR streams, types, opcodes, transactional semantics
│   ├── analysis/           # Cost models, liveness, dataflow analysis
│   ├── pass/               # Optimization passes (GVN, DCE, SIMD, speculative)
│   ├── graph/              # Execution graphs, regions, dependency tracking
│   ├── pipeline/           # Pass manager, analysis manager
│   ├── isa/                # Code generation (emitters, register allocation)
│   ├── jit/                # JIT runtime, orchestration, execution
│   └── all.hpp             # Umbrella header (single-include convenience)
│
├── src/                    # Source implementations
│   ├── main.cpp            # Production driver
│   ├── apps/
│   │   └── smoke_main.cpp  # Smoke/conformance/benchmark driver
│   ├── ir/
│   ├── analysis/
│   ├── core/
│   └── [other modules]/
│
├── tests/                  # Comprehensive test suites
│   ├── functional/         # Correctness tests (25+ pipeline tests)
│   ├── latency/            # Micro-benchmarks (cache, branch, ghost loads)
│   ├── performance/        # Performance stress tests
│   └── robustness/         # Edge cases, safety invariants
│
├── docs/                   # Public documentation
│   ├── TESTING_INDEX.md
│   ├── VERIFICATION_GUIDE.md
│   ├── business/
│   │   ├── DGTL_BUSINESS_MODEL.md
│   │   ├── DGTL_PRICING_AND_PILOT_OFFER.md
│   │   ├── DGTL_ONE_PAGE_SALES_DECK.md
│   │   ├── DGTL_WEBSITE_LANDING_COPY.md
│   │   ├── DGTL_COMMERCIAL_PROPOSAL_TEMPLATE.md
│   │   ├── DGTL_BRANDED_PROPOSAL.md
│   │   ├── DGTL_HFT_PROPOSAL.md
│   │   ├── DGTL_AI_RUNTIME_PROPOSAL.md
│   │   ├── DGTL_DSL_ENTERPRISE_LANGUAGE_PROPOSAL.md
│   │   ├── PROPRIETARY_LICENSE_AND_EVALUATION_DRAFT.md
│   │   └── PRODUCT_POSITIONING.md
│   └── analysis/
│       ├── DGPM_SPEC.md
│       ├── V1_2_POLYHEDRAL_EXECUTION_BACKLOG.md
│       ├── V1_2_POLYHEDRAL_PARALLELIZATION_ROADMAP.md
│       └── LANGUAGE_BACKEND_USAGE.md
│
│   ├── My_Docs/            # Detailed technical documentation (generated)
│   │   ├── core/           # Arena, Context, Symbols, Guards
│   │   ├── ir/             # IR format, streams, dialect rules
│   │   ├── analysis/       # Cost models, liveness, dataflow
│   │   ├── pass/           # Pass designs, transformation rules
│   │   ├── isa/            # Emitter specs, register allocation
│   │   └── jit/            # JIT design, orchestration
│
├── dgtl.ps1                # Main command runner
├── dgtl.cmd                # Windows shim for the command runner
├── LICENSE                 # Proprietary software notice
├── README.md               # This file
└── docs/
    ├── RELEASE_NOTES.md              # Release changelog
    ├── DEPENDENCY_VERSION_CONTROL.md  # Locked tool/platform versions
```

## Release Readiness

See [prelaunch.md](docs/My_Docs/zen_misc/prelaunch.md) for current gate status and open issues before broader rollout.

## Evaluation Access And Technical Review

This repository is not managed as an open contribution project.

### Access Path

1. Request evaluation or commercial access by email: **licensing@dgtl.tech**
2. Define scope: backend targets, performance scenarios, and evaluation duration.
3. Execute under written terms before source sharing or production usage.

### Technical Review Policy

Technical depth is shared using a black-box model:

- architecture and algorithm narratives
- benchmark methodology and reproducible test settings
- DGTL input to assembly output demonstrations

Implementation details of internal passes, heuristics, and lowering internals remain proprietary.

### Source Header Compliance

Use the short proprietary header in source files and verify coverage with:

```powershell
.\tools\scripts\verify_proprietary_headers.ps1
```

Compliance is also enforced automatically by validation commands:

- `dgtl run-all`
- `dgtl run-strict-conformance`
- `dgtl update-goldens` (and `strict` mode)
- `dgtl test` / `dgtl test full`
- `dgtl clean-build-and-run`
- `dgtl header-check`

Useful modes:

```powershell
# Summary only (non-verbose on failures)
.\tools\scripts\verify_proprietary_headers.ps1 -SummaryOnly

# CI-friendly JSON to stdout
.\tools\scripts\verify_proprietary_headers.ps1 -Json

# CI-friendly JSON written to file
.\tools\scripts\verify_proprietary_headers.ps1 -Json -JsonOut build\logs\header-compliance.json

# Restrict scope or exclusions when needed
.\tools\scripts\verify_proprietary_headers.ps1 -Include *.hpp,*.cpp -ExcludeRegex "[\\/]third_party[\\/]"
```

## Troubleshooting

### Build Issues

**`g++: command not found`**
- Install GCC 15.2.0+ via MSYS2 (Windows) or system package manager (Linux/macOS)

**`dgtl: command not found`**
- Re-run `./tools/scripts/install_dgtl_command.ps1`
- Open a new PowerShell session and confirm the command exists with `dgtl help`

**Compilation hangs**
- Run `dgtl clean` and rebuild from a fresh state
- Check for recursive includes in headers (use `#pragma once`)

**Strict build fails**
- Run `dgtl strict` directly to surface warning-as-error failures
- Fix the reported warnings before refreshing strict baselines with `dgtl update-golden strict`

### Test Failures

**Tests timeout**
- Run a quick single-backend smoke check first: `dgtl run arm64`
- Check ulimit (Linux): `ulimit -a` should have sufficient stack

**Assertion failures in transactional code**
- Check whether shadow buffer initialization succeeded
- Verify arena capacity: `DGTL_Arena(size_bytes)` should allocate sufficient memory

### Runtime Errors

**Segmentation fault in code emission**
- Verify the IRStream was populated before emission
- Check that Target.hpp specifies a valid backend

**Intent hash mismatch**
- This indicates a malformed IR mutation (likely in a pass)
- Enable verbose output to trace the problematic node

## License & Attribution

See project root for licensing information.

## Community & Support

For questions, issues, or discussions:
1. Review [prelaunch.md](prelaunch.md) for known limitations
2. Check [docs/My_Docs/](docs/My_Docs/) for detailed explanations
3. Run verification tests from [VERIFICATION_GUIDE.md](docs/VERIFICATION_GUIDE.md)


before release :: 
# 1. Clean slate
dgtl clean-all

# 2. Production build
dgtl build-release

# 3. Aggregate test suite (functional + robustness + performance)
dgtl test full

# 4. Full backend conformance (arm64, x64, nasm, wasm)
dgtl run-all

# 5. Strict build (-Wall -Wextra -Wpedantic -Werror)
dgtl build-strict

# 6. Strict conformance gate
dgtl run-strict-conformance

# 7. Proprietary header compliance
dgtl header-check

# 8. Full performance benchmark gate
dgtl brun

# 9. SDK package (compiles lib, runs smoke link test, creates zip)
dgtl package

======= or ===== following ::

dgtl clean-all
dgtl test full
dgtl run-all
dgtl run-strict-conformance
dgtl brun
dgtl package