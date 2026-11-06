# DGTL Command Execution Guide - Optimized Sequence with Zero File Overlap

**Generated:** May 13, 2026  
**Principle:** Each command owns specific files/directories with ZERO overlap  
**Scope:** Commands EXCLUDING: `test`, `brun`, `strict`, `smoke-build`, `build`, `build-release`, `strict-conformance`, `package`

---

## Executive Summary: Correct Command Sequence

```powershell
# PHASE 0: PREPARATION (Stateless header validation - can run anytime)
dgtl header-check

# PHASE 1: ONE-TIME BASELINE ESTABLISHMENT (Setup golden references)
dgtl update-golden

# PHASE 2: CONTINUOUS VALIDATION (Compare against baseline)
dgtl run                # Full conformance (all 13 backends)
dgtl run-backend x64    # Single backend (optional, for iteration)
dgtl run-backend arm64  # Single backend (optional, for iteration)

# PHASE 3: PERFORMANCE PROFILING (Optional - requires strict binary)
dgtl bench x64 fold
dgtl bench arm64 simd
dgtl bench x64 pressure 256

# PHASE 4: CROSS-BACKEND COMPILATION (Payload distribution)
dgtl compile

# NOTE: crun (clean-build-and-run) is a COMPOSITE - avoid in normal workflow
```

---

## File Ownership Matrix: ZERO OVERLAP GUARANTEE

| Command | Input Files | Output Files | Purpose |
|---------|------------|--------------|---------|
| **header-check** | `src/`, `include/`, `tests/`, `tools/` (sources only) | `build/logs/header-compliance.ok` | Static validation |
| **update-golden** | `build/dgtl_main.exe` | `docs/My_Docs/commands/golden/*.golden.txt` | Baseline creation |
| **run** | `docs/My_Docs/commands/golden/*.golden.txt` | `build/logs/conformance_*.log` | Conformance test |
| **bench** | `build/dgtl_main_strict.exe` | stdout (console output) | Performance metrics |
| **compile** | `build/dgtl_release.exe`, `build/tests/frontend_payload_io.dgtl.payload` | `build/out/payload.*` | Backend distribution |

**✓ NO OVERLAPS CONFIRMED:**
- Source files: Only read by `header-check`
- Golden files: Only written by `update-golden`, only read by `run`
- Conformance logs: Only written by `run`
- Benchmark output: Stdout only (no file conflict)
- Compiled payloads: Only in `build/out/`, not used elsewhere

---

## PHASE 0: Preparation - Header Validation

### **Command: `header-check` (alias: `check-headers`)**

**Purpose:** Validate all source code files contain required proprietary copyright header.  
**Status:** Stateless, can execute anytime (read-only validation)  
**Dependencies:** None

#### Input Files (READ ONLY):
```
src/
  ├── main.cpp
  ├── apps/
  │   ├── smoke_main.cpp
  │   └── **/*.cpp
  ├── analysis/**/*.cpp
  ├── graph/**/*.cpp
  ├── ir/**/*.cpp
  ├── /**/*.cpp
  ├── pass/**/*.cpp
  ├── pipeline/**/*.cpp
  ├── runtime/**/*.cpp
  └── sdk/**/*.cpp

include/dgtl/
  ├── **/*.hpp
  ├── **/*.h
  └── **/*.hxx

tests/
  ├── test_suite_main.cpp
  ├── functional/**/*.cpp
  ├── functional/**/*.hpp
  ├── latency/**/*.cpp
  ├── performance/**/*.cpp
  ├── performance/**/*.hpp
  ├── sdk/**/*.cpp
  └── tools/**/*.cpp

tools/
  └── scripts/*.ps1
```

#### Output Files (WRITE):
```
build/logs/header-compliance.ok          # Cache stamp (created if all headers valid)
```

#### Required Header:
```
Copyright (c) 2026 TataG Systems. All rights reserved.
```

#### Validation Script:
```
tools/scripts/verify_proprietary_headers.ps1
```

#### Execution:
```powershell
dgtl header-check        # Validate all headers

# Example output:
# [compliance] Proprietary header check reused cached pass
# OR
# Missing proprietary header in X file(s) out of Y scanned:
#  - src/some_file.cpp
```

#### Use Cases:
- Before committing code
- CI/CD pre-gate
- Audit compliance
- Anytime (no side effects)

---

## PHASE 1: One-Time Baseline - Golden Reference Creation

### **Command: `update-golden` (alias: `update-goldens`)**

**Purpose:** Create or refresh golden baseline files for all 13 backends.  
**Status:** Run ONCE during development, then only when specs change  
**Dependencies:** `header-check` MUST pass first  
**Executables Required:** `build/dgtl_main.exe` (smoke binary) or `build/dgtl_main_strict.exe` (strict)

#### Input Files (READ):
```
build/dgtl_main.exe                                    # Smoke binary (faster, iteration-friendly)
  OR
build/dgtl_main_strict.exe                             # Strict binary (all warnings as errors)

Execution Script:
docs/My_Docs/commands/backend_conformance_check.ps1
  └── Invoked with: -UpdateGolden flag
```

#### Output Files (WRITE - Creates/Overwrites):
```
docs/My_Docs/commands/golden/
  ├── arm64.golden.txt                                 # ARM64 backend output
  ├── x64.golden.txt                                   # x64 backend output
  ├── nasm.golden.txt                                  # NASM backend output
  ├── wasm.golden.txt                                  # WebAssembly backend output
  ├── arm32.golden.txt                                 # ARM32 backend output
  ├── riscv64.golden.txt                               # RISC-V 64 backend output
  ├── cuda.golden.txt                                  # CUDA backend output
  ├── rocm.golden.txt                                  # ROCm backend output
  ├── macho.golden.txt                                 # macOS Mach-O backend output
  ├── elfobj.golden.txt                                # ELF object backend output
  ├── staticlib.golden.txt                             # Static library backend output
  ├── coffobj.golden.txt                               # COFF object backend output
  └── cofflib.golden.txt                               # COFF library backend output
```

#### Execution:
```powershell
# Using smoke binary (default, faster iteration):
dgtl update-golden

# Using strict binary (maximum validation):
dgtl update-golden strict

# Example output:
# [CHECK] arm64
# [CHECK] x64
# ...
# =================================================================
# === DGTL Backend Conformance Summary ===
# PASSED: 13/13
# =================================================================
```

#### 13 Backends Tested During Update:
1. **arm64** - ARM 64-bit architecture
2. **x64** - x86-64 architecture
3. **nasm** - NASM assembly format
4. **wasm** - WebAssembly
5. **arm32** - ARM 32-bit architecture
6. **riscv64** - RISC-V 64-bit architecture
7. **cuda** - NVIDIA CUDA
8. **rocm** - AMD ROCm
9. **macho** - macOS Mach-O format
10. **elfobj** - ELF object format
11. **staticlib** - Static library format
12. **coffobj** - COFF object format
13. **cofflib** - COFF library format

#### Validation Checks per Backend (from script):
```
✓ MustContain: Required text/keywords in output
✓ MustNotContain: Forbidden error messages
✓ Deterministic hashing: Output consistency check
✓ Logic hash capture: IR semantics validation
```

#### Critical Notes:
- ⚠️ This CREATES the baseline - any golden file updated here becomes the new reference
- ⚠️ Only run when backend implementations are STABLE
- ⚠️ Commit updated golden files to version control after verification
- ✓ Safe to re-run: overwrites previous baseline

---

## PHASE 2: Continuous Validation - Conformance Testing

### **Command: `run` (aliases: `run-all`, `run-backend`)**

**Purpose:** Execute backend conformance validation against golden baselines.  
**Status:** Repeatable validation (main testing command)  
**Dependencies:** `update-golden` MUST run first  
**Executables Required:** `build/dgtl_main.exe` (smoke binary)

#### Input Files (READ ONLY):
```
docs/My_Docs/commands/golden/
  ├── arm64.golden.txt
  ├── x64.golden.txt
  ├── nasm.golden.txt
  ├── wasm.golden.txt
  ├── arm32.golden.txt
  ├── riscv64.golden.txt
  ├── cuda.golden.txt
  ├── rocm.golden.txt
  ├── macho.golden.txt
  ├── elfobj.golden.txt
  ├── staticlib.golden.txt
  ├── coffobj.golden.txt
  └── cofflib.golden.txt

Execution Script:
docs/My_Docs/commands/backend_conformance_check.ps1
  └── Invoked with: -Executable <path>

Cache:
build/logs/conformance_dgtl_main.cache                 # Fingerprint for fast skip
```

#### Output Files (WRITE):
```
build/logs/
  ├── conformance_arm64.log                            # arm64 test results
  ├── conformance_x64.log                              # x64 test results
  ├── conformance_nasm.log                             # nasm test results
  ├── conformance_wasm.log                             # wasm test results
  ├── conformance_arm32.log                            # arm32 test results
  ├── conformance_riscv64.log                          # riscv64 test results
  ├── conformance_cuda.log                             # cuda test results
  ├── conformance_rocm.log                             # rocm test results
  ├── conformance_macho.log                            # macho test results
  ├── conformance_elfobj.log                           # elfobj test results
  ├── conformance_staticlib.log                        # staticlib test results
  ├── conformance_coffobj.log                          # coffobj test results
  ├── conformance_cofflib.log                          # cofflib test results
  └── conformance_dgtl_main.cache                      # Fingerprint cache (skip if unchanged)
```

#### Execution Modes:

**Full Conformance (all 13 backends):**
```powershell
dgtl run
dgtl run-all

# Example output:
# [cache] Conformance up-to-date. Skipping backend runs.
# Conformance PASSED for all backends.
# OR
# [CHECK] arm64
# [PASS] arm64 :: output matches golden
# [CHECK] x64
# [PASS] x64 :: output matches golden
# ... (11 more)
# [FINAL] 13/13 backends PASSED
```

**Single Backend (for iteration):**
```powershell
dgtl run-backend x64                 # Test only x64
dgtl run-backend arm64               # Test only ARM64
dgtl run-backend wasm                # Test only WebAssembly
# ... any of the 13 backends

# Example output:
# [CHECK] x64
# [PASS] x64 :: output matches golden
```

#### Cache Behavior:
- **Cache Hit:** If exe + golden files unchanged → skips all 13 backends, instant pass
- **Cache Miss:** Runs all 13 backends if any input changed
- **Cache Location:** `build/logs/conformance_dgtl_main.cache`

#### Expected Results:
```
✓ PASS: 13/13 backends (all outputs match golden)
✗ FAIL: X/13 backends (diff highlighted in logs)
```

---

## PHASE 3: Performance Profiling - Backend Benchmarking

### **Command: `bench` (alias: `bench-backend`)**

**Purpose:** Profile backend performance with configurable optimization profiles.  
**Status:** Optional performance validation  
**Dependencies:** None (independent)  
**Executables Required:** `build/dgtl_main_strict.exe` (strict binary with -Wall -Wextra -Werror)

#### Input Files (READ):
```
build/dgtl_main_strict.exe                             # Strict binary (all warnings as errors)

Execution Script:
docs/My_Docs/commands/backend_conformance_check.ps1
  └── Invoked with: -Executable <strict exe> --benchmark
```

#### Output Files (WRITE):
```
stdout (console output)                                # Real-time benchmark metrics
  └── No file artifacts (ephemeral output)

Optional Log Redirection:
build/logs/benchmark_<backend>_<profile>.log           # If manually redirected
```

#### Benchmark Profiles (5 modes):

| Profile | Purpose | Flag | Example |
|---------|---------|------|---------|
| **fold** | Constant folding optimization | `--benchmark-profile fold` | `dgtl bench x64 fold` |
| **fusion** | Operation fusion | `--benchmark-profile fusion` | `dgtl bench x64 fusion` |
| **simd** | SIMD vectorization | `--benchmark-profile simd` | `dgtl bench arm64 simd` |
| **pressure** | Register pressure (requires var count) | `--pressure-vars <n>` | `dgtl bench x64 pressure 256` |
| **predication** | Predication patterns | `--benchmark-profile predication` | `dgtl bench x64 predication` |

#### Execution Variants:

**Profile-Based Benchmarking:**
```powershell
# Constant folding across x64
dgtl bench x64 fold

# Fusion across ARM64
dgtl bench arm64 fusion

# SIMD vectorization across x64
dgtl bench x64 simd

# Register pressure with 256 variables
dgtl bench x64 pressure 256

# Predication patterns
dgtl bench x64 predication
```

**Iteration-Based Benchmarking:**
```powershell
# Run 1000 iterations
dgtl bench x64 1000

# Run 5000 iterations
dgtl bench x64 5000
```

**Advanced Options:**
```powershell
# Dump detailed benchmark data
dgtl bench x64 fold dump

# With polyhedral optimization flags
dgtl bench x64 --polyhedral-mode safe --polyhedral-tile 32 --polyhedral-parallel auto

# Combine profile + iterations
dgtl bench arm64 fusion 1000
```

#### Valid Backends (13):
```
arm64, x64, nasm, wasm, arm32, riscv64, cuda, rocm, macho, elfobj, staticlib, coffobj, cofflib
```

#### Output Metrics:
```
[BENCHMARK] x64 :: fold profile
  avg_us=<time>      # Average microseconds
  opt_us=<time>      # Optimization pass time
  emit_us=<time>     # Code emission time
  total_us=<time>    # Total time
  [PASS] metric threshold met
```

#### Use Cases:
- Performance regression detection
- Backend optimization validation
- Comparative analysis (x64 vs arm64)
- CI/CD performance gates

---

## PHASE 4: Cross-Backend Distribution - Payload Compilation

### **Command: `compile` (alias: `compile-all`)**

**Purpose:** Compile a single frontend payload to all 13 backend targets.  
**Status:** Verification of multi-backend compilation pipeline  
**Dependencies:** None (independent)  
**Executables Required:** `build/dgtl_release.exe` (production binary)

#### Input Files (READ):
```
build/dgtl_release.exe                                 # Production binary

Input Payloads (one of):
build/tests/frontend_payload_io.dgtl.payload           # Default payload
  OR
custom_payload.dgtl.payload                            # User-specified payload

Execution Script:
docs/My_Docs/commands/backend_conformance_check.ps1
  └── Invoked with: --backend <name> (for each of 13)
```

#### Output Files (WRITE):
```
build/out/
  ├── payload.arm64.s                                  # ARM64 assembly
  ├── payload.x64.bin                                  # x64 binary
  ├── payload.nasm.asm                                 # NASM assembly
  ├── payload.wasm                                     # WebAssembly module
  ├── payload.arm32.s                                  # ARM32 assembly
  ├── payload.riscv64.s                                # RISC-V 64 assembly
  ├── payload.cuda.ptx                                 # CUDA PTX code
  ├── payload.rocm.gcn                                 # ROCm GCN code
  ├── payload.macho.o                                  # macOS Mach-O object
  ├── payload.elfobj.o                                 # ELF object file
  ├── payload.staticlib.a                              # Static library archive
  ├── payload.coffobj.obj                              # COFF object file
  └── payload.cofflib.lib                              # COFF library archive
```

#### Execution:

**Default Payload (standard test):**
```powershell
dgtl compile

# Example output:
# [compile] Processing 13 backends...
# [compile] arm64 -> payload.arm64.s (1024 bytes)
# [compile] x64 -> payload.x64.bin (2048 bytes)
# ... (11 more)
# [compile] cofflib -> payload.cofflib.lib (4096 bytes)
# [compile] All 13 backends compiled successfully
```

**Custom Payload:**
```powershell
# Absolute path
dgtl compile /path/to/custom.dgtl.payload

# Relative path
dgtl compile tests/payloads/special_test.dgtl.payload

# From build/tests
dgtl compile build/tests/frontend_payload_io.dgtl.payload
```

#### Output Format per Backend:

| Backend | Extension | Type | Size Range |
|---------|-----------|------|------------|
| arm64 | `.s` | Assembly | 0.5-4 KB |
| x64 | `.bin` | Binary | 1-8 KB |
| nasm | `.asm` | Assembly | 0.5-4 KB |
| wasm | `.wasm` | Binary (WebAssembly) | 1-16 KB |
| arm32 | `.s` | Assembly | 0.5-4 KB |
| riscv64 | `.s` | Assembly | 0.5-4 KB |
| cuda | `.ptx` | PTX code | 2-32 KB |
| rocm | `.gcn` | GCN code | 2-32 KB |
| macho | `.o` | Object | 0.5-4 KB |
| elfobj | `.o` | Object | 0.5-4 KB |
| staticlib | `.a` | Archive | 5-50 KB |
| coffobj | `.obj` | Object | 0.5-4 KB |
| cofflib | `.lib` | Library | 5-50 KB |

#### Use Cases:
- Verify all backends can compile same payload
- Distribution artifact generation
- Cross-platform testing
- Integration with downstream tools

---

## DO NOT USE: `crun` (clean-build-and-run)

### **Why Avoid `crun`:**
```powershell
dgtl clean-build-and-run  # ❌ COMPOSITE COMMAND - avoid in workflows
```

**Problem:** Combines multiple operations with implicit state changes:
1. **Deletes:** All exe, libs, logs (destructive)
2. **Rebuilds:** Fresh binary
3. **Runs:** Conformance

**Why problematic:**
- ❌ Wipes golden files cache
- ❌ Invalidates all phase caching
- ❌ Not reproducible (deletes intermediate state)
- ❌ Mixes concerns

**Correct alternative:**
```powershell
# Explicit, traceable sequence:
dgtl clean-all              # ← separate clean phase
dgtl build                  # ← separate build phase
dgtl run                    # ← separate validation phase
```

---

## Complete Execution Sequence with Commands

### **Scenario 1: Fresh Development (First Time)**

```powershell
# Step 1: Validate headers (no build needed)
dgtl header-check
# Expected: "Proprietary header check passed"

# Step 2: Establish baselines (one-time)
dgtl update-golden
# Expected: "13/13 backends PASSED"

# Step 3: Verify against baselines
dgtl run
# Expected: "[cache] Conformance up-to-date" OR "13/13 backends PASSED"

# Step 4: Optional - benchmark a specific backend
dgtl bench x64 fold
# Expected: Performance metrics displayed

# Step 5: Compile to all backends
dgtl compile
# Expected: "All 13 backends compiled successfully"
```

**Files Written:**
```
docs/My_Docs/commands/golden/     # 13 files created (FIRST TIME ONLY)
build/logs/header-compliance.ok    # Cache
build/logs/conformance_*.log       # 13 logs
build/out/payload.*                # 13 compiled artifacts
```

### **Scenario 2: Iterative Development (Source Code Changes)**

```powershell
# Step 1: Check headers (always safe)
dgtl header-check

# Step 2: Validate against golden (most common)
dgtl run
# Expected: "[cache] Conformance up-to-date" or failures if output changed

# Step 3: If backend output changed, inspect logs
cat build/logs/conformance_x64.log
# Review diff between old golden and new output

# Step 4: If changes are CORRECT, update golden
dgtl update-golden

# Step 5: Re-validate against new golden
dgtl run
# Expected: "13/13 backends PASSED"

# Step 6: Benchmark specific backend for regression
dgtl bench x64 simd
```

**Files Modified:**
```
docs/My_Docs/commands/golden/*.golden.txt  # Only if intentional via update-golden
build/logs/conformance_*.log                # Always updated by run
```

### **Scenario 3: Pre-Release Gate (Full Validation)**

```powershell
# Phase 0: Preparation
dgtl header-check
# Status: All headers present ✓

# Phase 1: Baseline (skip if already done)
# dgtl update-golden  # Only if baseline changed

# Phase 2: Conformance
dgtl run
dgtl run-backend x64          # Deep dive on x64
dgtl run-backend arm64        # Deep dive on ARM64
# Status: All backends pass ✓

# Phase 3: Benchmarks
dgtl bench x64 fold
dgtl bench arm64 fusion
dgtl bench x64 pressure 256
# Status: No performance regressions ✓

# Phase 4: Distribution
dgtl compile
# Status: All 13 backends compile ✓

# RELEASE READY ✓
```

---

## File Ownership Summary: ZERO OVERLAP

```
SOURCE CODE (read-only):
├── src/                          → ONLY read by header-check
├── include/                       → ONLY read by header-check
├── tests/                         → ONLY read by header-check
└── tools/                         → ONLY read by header-check

GOLDEN BASELINES (write-once + read-many):
└── docs/My_Docs/commands/golden/  → ONLY written by update-golden
                                     ONLY read by run

LOGS & ARTIFACTS (command-specific):
├── build/logs/header-*.ok          → ONLY written by header-check
├── build/logs/conformance_*.log    → ONLY written by run
├── build/logs/*.cache              → ONLY written/read by respective commands
└── build/out/payload.*             → ONLY written by compile

PERFORMANCE OUTPUT:
└── stdout (ephemeral)             → bench (no file conflict)
```

✓ **GUARANTEED NO FILE CONFLICTS**

---

## Command Reference Table

| Phase | Command | Files Input | Files Output | Dependencies | Time |
|-------|---------|-------------|--------------|--------------|------|
| 0 | header-check | src/, include/, tests/ | header-compliance.ok | None | <10s |
| 1 | update-golden | dgtl_main.exe | 13 golden files | header-check | 1-2min |
| 2 | run | 13 golden files | 13 conformance logs | update-golden | 30-60s |
| 2 | run-backend x64 | 1 golden file | 1 log | update-golden | 5-10s |
| 3 | bench x64 | dgtl_main_strict.exe | stdout | None | 10-30s |
| 4 | compile | dgtl_release.exe | 13 payloads | None | 30-60s |

---

## Critical Notes

✓ **Always Run in Order:** Phase 0 → Phase 1 (once) → Phase 2+ (continuous)  
✓ **No File Conflicts:** Each command owns specific files, zero overlap  
✓ **Caching Smart:** `run` skips if inputs unchanged (instant pass)  
✓ **Stateless where possible:** `header-check`, `bench`, `compile` are independent  
✗ **Never mix:** Avoid `crun` in CI/CD workflows (use explicit phases)  
✗ **Golden integrity:** Only update when backend output change is INTENTIONAL  

---

## Appendix: All 13 Backends Reference

1. **arm64** - ARM64 architecture (64-bit ARM)
2. **x64** - x86-64 architecture (Intel/AMD 64-bit)
3. **nasm** - NASM assembler format
4. **wasm** - WebAssembly (browser/WASI runtime)
5. **arm32** - ARM 32-bit architecture
6. **riscv64** - RISC-V 64-bit architecture
7. **cuda** - NVIDIA CUDA GPU compute
8. **rocm** - AMD ROCm GPU compute
9. **macho** - macOS Mach-O object format
10. **elfobj** - ELF object format (Linux/Unix)
11. **staticlib** - Static library archive format
12. **coffobj** - COFF object format (Windows)
13. **cofflib** - COFF library format (Windows)
