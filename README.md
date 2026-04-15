# DGTL: High-Performance Compiler Infrastructure

**DGTL** is a retargetable compiler infrastructure designed for high-performance code generation and optimization. It targets **AI inference, high-frequency trading (HFT), cloud computing, and embedded systems** requiring low-latency, vectorized code generation.

## Core Features

- **Bit-Packed Intermediate Representation (BIR 2.1)**: Compact, SIMD-friendly IR with transactional semantics
- **Transactional Memory Model**: Thread-local arenas with speculative execution and rollback via shadow buffers
- **AI-Guided Optimization**: Neural network-based cost prediction for intelligent pass ordering
- **Multi-Target Code Generation**: ARM64/AArch64, x86-64, WebAssembly, NASM assembly
- **Advanced Optimization Passes**: 
  - Global Value Numbering (GVN) with SIMD fingerprinting
  - Dead Code Elimination, Constant Folding
  - SIMD legalization, vectorization, and gather/scatter optimization
  - Loop unswitching, polyhedral transformations, speculative inlining
  - Memory dependency analysis and redundant store fusion
- **JIT Execution Runtime**: W^X-protected code execution with multi-architecture orchestration
- **Graph-Based Analysis**: Execution graphs, dependency tracking, region-level optimization

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

Builds the default smoke and conformance driver at `build\dgtl_main.exe`.

```powershell
dgtl strict
```

Builds a strict variant at `build\dgtl_main_strict.exe` using warning-heavy flags and `-Werror`.

## Command Reference

All commands below are run from the repository root using the PowerShell `dgtl` function.

### Quick Command Reference

Use these short forms for day-to-day workflows:

**General**

```powershell
dgtl help
```

**Build**

```powershell
dgtl build
dgtl strict
```

**Run / Validate**

```powershell
dgtl run
dgtl run arm64
dgtl run x64
dgtl run nasm
dgtl run wasm
```

**Benchmark**

```powershell
dgtl bench x64
dgtl bench x64 1000
dgtl bench x64 fusion
dgtl bench x64 simd
dgtl bench x64 pressure 64
dgtl bench x64 fusion dump
dgtl bench wasm simd
dgtl bench nasm fusion dump
```

**Golden Baselines**

```powershell
dgtl update-golden
dgtl update-golden strict
```

**Strict Conformance**

```powershell
dgtl srun
```

**Cleanup / One-Shot Flow**

```powershell
dgtl clean
dgtl crun
```

| Command | Requires existing build? | What it does |
|---|---|---|
| `dgtl help` | No | Prints command usage and examples. |
| `dgtl build` | No | Builds `build\dgtl_main.exe`. |
| `dgtl strict` | No | Builds `build\dgtl_main_strict.exe` with strict warnings and `-Werror`. |
| `dgtl run` | Yes (`build\dgtl_main.exe`) | Runs full backend conformance (`arm64`, `x64`, `nasm`, `wasm`) against goldens. |
| `dgtl run <backend>` | Yes (`build\dgtl_main.exe`) | Runs a single backend smoke path (`arm64`, `x64`, `nasm`, `wasm`). |
| `dgtl bench <backend> [profile\|iterations] [pressure-vars] [dump]` | Auto-builds strict if missing | Runs strict benchmark mode for one backend with profile and optional dump output. |
| `dgtl update-golden` | Auto-builds default if missing | Refreshes default conformance golden files intentionally. |
| `dgtl update-golden strict` | Auto-builds strict if missing | Refreshes strict conformance golden files intentionally. |
| `dgtl srun` | Yes (`build\dgtl_main_strict.exe`) | Runs full backend conformance using strict executable. |
| `dgtl clean` | No | Deletes DGTL build outputs and conformance log files. |
| `dgtl crun` | No | Executes `clean -> build -> run` in one command. |

### Command Runner Flags

- **Default build**: `-std=c++26 -O2 -I ./include`
- **Strict build**: `-std=c++26 -O2 -I ./include -Wall -Wextra -Wpedantic -Werror`
- **Custom tuning**: If you need flags such as `-march=native` or `-flto`, add them in your own local build flow rather than assuming the `dgtl` wrapper uses them.

## Running Tests

### Option 1: Run Full Backend Validation

```powershell
dgtl run
```

Runs the conformance gate across `arm64`, `x64`, `nasm`, and `wasm` using `build\dgtl_main.exe`, then compares normalized output against stored goldens.

If `build\dgtl_main.exe` is missing, run:

```powershell
dgtl build
```

### Option 2: Run One Backend Only

```powershell
dgtl run arm64
```

Replace `arm64` with `x64`, `nasm`, or `wasm` to run a quick single-backend smoke check without invoking the full conformance gate.

If `build\dgtl_main.exe` is missing, run `dgtl build` first.

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

### Combined Workflow

```powershell
dgtl clean
dgtl build
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

For repository validation, prefer the built-in command flow instead of ad hoc compile commands:

```powershell
dgtl build
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
Graph Scheduling (Region-level, multi-backend aware)
  ↓
ISA Emission (ARM64 / x86-64 / WASM / NASM)
  ↓
JIT Execution (Optional: W^X-protected runtime)
```

### Supported Backends

| Backend | Status | Use Case |
|---------|--------|----------|
| **ARM64 (AArch64)** | ✅ Primary | Mobile, edge, AWS Graviton |
| **x86-64** | ✅ Primary | Intel/AMD servers, HFT workstations |
| **WebAssembly** | ✅ Complete | Browser, cloud-native portability |
| **NASM** | ✅ Complete | Traditional assembly toolchains |

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
- `Stack_Spiller.hpp`: Spill code generation

### JIT Runtime (`include/dgtl/jit/`)
- `JIT_Executor.hpp`: W^X memory protection, cross-platform I-Cache sync
- `MultiArch_Orchestrator.hpp`: Multi-target JIT dispatch
- `Hardware_Scheduler.hpp`: Hardware-aware execution scheduling
- `Async_Speculator.hpp`: Asynchronous speculation support

## Documentation

- [BitPacked_IR_Core.md](docs/analysis/DGPM_SPEC.md): IR architecture deep-dive
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
- [PRODUCT_POSITIONING.md](docs/business/PRODUCT_POSITIONING.md): Customer-facing positioning and messaging
- [LICENSE](LICENSE): Release-facing proprietary software notice
- [DEPENDENCY_VERSION_CONTROL.md](DEPENDENCY_VERSION_CONTROL.md): Locked tool versions
- [My_Docs/](My_Docs/): Detailed module documentation and examples

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
│   ├── main.cpp            # Smoke test driver (demo usage)
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
│       └── LANGUAGE_BACKEND_USAGE.md
│
├── My_Docs/                # Detailed technical documentation (generated)
│   ├── core/               # Arena, Context, Symbols, Guards
│   ├── ir/                 # IR format, streams, dialect rules
│   ├── analysis/           # Cost models, liveness, dataflow
│   ├── pass/               # Pass designs, transformation rules
│   ├── isa/                # Emitter specs, register allocation
│   └── jit/                # JIT design, orchestration
│
├── dgtl.ps1                # Main command runner
├── dgtl.cmd                # Windows shim for the command runner
├── LICENSE                 # Proprietary software notice
├── README.md               # This file
├── DEPENDENCY_VERSION_CONTROL.md  # Locked tool/platform versions
└── prelaunch.md            # Prelaunch review findings
```

## Release Readiness

See [prelaunch.md](prelaunch.md) for current gate status and open issues before broader rollout.

## Contributing

### Before You Start

1. **Read the code**: Understand the module layout in `include/dgtl/`
2. **Run validation**: Use `dgtl run` on the supported backends before opening a PR
3. **Check documentation**: Use `My_Docs/` for detailed component info

### Contribution Workflow

1. **Create a branch** for your feature or fix
2. **Write tests first** for any new functionality (in `tests/functional/` or `tests/performance/`)
3. **Add/update headers** (C++ implementation goes in `include/dgtl/`, source in `src/`)
4. **Update documentation** if adding new passes, opcodes, or APIs
5. **Verify all tests pass** before opening a PR:
  ```powershell
  dgtl clean
  dgtl build
  dgtl run
   ```

### Code Standards

- **C++26**: Use C++26 features where applicable (concepts, ranges, etc.)
- **Zero-overhead**: Avoid exceptions and RTTI; use noexcept where possible
- **Transactionality**: New IR mutations should work with shadow buffers
- **Const correctness**: IR streams and analyses should mark const where appropriate

### Areas for Contribution

- **New backends**: Add emitters (refer to `ARM64_Emitter.hpp` and `X64_Strike_Emitter.hpp`)
- **New optimization passes**: Extend `include/dgtl/pass/` with new transforms
- **Cost model tuning**: Improve `AI_Cost_Model.hpp` weights and features
- **Test coverage**: Add robustness tests in `tests/robustness/`
- **Documentation**: Improve header comments and module guides in `My_Docs/`

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
2. Check [My_Docs/](My_Docs/) for detailed explanations
3. Run verification tests from [VERIFICATION_GUIDE.md](docs/VERIFICATION_GUIDE.md)