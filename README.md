/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL — Deep General-purpose Transformation Layer
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * This software is the proprietary sovereign property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

# DGTL 2.1.0: Sovereign Industry-Grade Compiler & JIT Engine

[![CI](https://github.com/TataG-Systems/DGTL/actions/workflows/dgtl_industrial_ci.yml/badge.svg)](https://github.com/TataG-Systems/DGTL/actions/workflows/dgtl_industrial_ci.yml)
[![Version](https://img.shields.io/badge/DGTL-2.1.0--SOVEREIGN-blue.svg)](https://github.com/TataG-Systems/DGTL)
[![Zero-Dependency](https://img.shields.io/badge/Dependencies-Zero%20External-success.svg)](https://github.com/TataG-Systems/DGTL)
[![Language](https://img.shields.io/badge/C%2B%2B-20%20%2F%2023%20%2F%2026-magenta.svg)](https://github.com/TataG-Systems/DGTL)
[![Package](https://img.shields.io/badge/Package-MYZON%20.pkg%20%2F%20.toml-orange.svg)](file:///D:/TataG/DGTL/dgtl.pkg)

**DGTL (Deep General-purpose Transformation Layer)** is an ultra-high-velocity, **100% zero-dependency**, frontend-agnostic sovereign compiler infrastructure and Just-In-Time (JIT) execution engine. Engineered from the silicon up by TataG Systems, DGTL replaces legacy heavy compiler backends with an autonomous, high-density transactional pipeline spanning intermediate representation, 26-pass optimization, SMT formal verification, instruction selection across 9 hardware ISAs, native binary linkers, and in-memory W^X machine code execution.

---

## 1. Architectural Highlights

- **100% Zero External Dependencies**: Completely independent of CMake, LLVM, GNU Make, Ninja, Python, and external linkers. Bootstrapped with a single compiler invocation.
- **Sovereign Package Standard**: Native integration with MYZON's package manager (`mypm` / `pm`) using standard `dgtl.pkg` and `dgtl.toml` manifests.
- **Cross-Platform Static & Dynamic Libraries**: Emits deterministic static archives (`dgtl.lib`, `libdgtl.a`) and dynamic shared libraries (`dgtl.dll`, `libdgtl.so`, `libdgtl.dylib`) across Windows, Linux, and macOS.
- **12-Layer Verification Architecture**: Comprehensive stack auditing covering frontend ingestion through live JIT execution.
- **Deterministic Bit-for-Bit Archives**: Employs timestamp and UID/GID normalization (`ar -Drcs`) guaranteeing reproducible hashes.
- **Production Footprint Stripping**: Built-in release stripping drops executable and library sizes by over 65% for lean edge and cloud deployments.
- **Formal SMT Proof Verification**: Invariant verification using bit-vector formal models to mathematically prove the semantics of compiler optimization passes.

---

## 2. 12-Layer Sovereign Compiler Pipeline

```
                                  [ Source Code / AST / DSL ]
                                               │
 ┌─────────────────────────────────────────────▼────────────────────────────────────────────┐
 │  LAYER 1: Polyglot Frontend Ingestion (MYZON AST, C-ABI, Textual IR, High-Level DSL)      │
 ├──────────────────────────────────────────────────────────────────────────────────────────┤
 │  LAYER 2: Transactional SoA IRStream & Arena Megabuffer (Graph SSA, Dominance Frontiers) │
 ├──────────────────────────────────────────────────────────────────────────────────────────┤
 │  LAYER 3: SMT Formal Equivalence & Sentinel Proof Gate (Z3/Bit-Vector Semantics Proof)   │
 ├──────────────────────────────────────────────────────────────────────────────────────────┤
 │  LAYER 4: 26 Optimization Passes Pipeline & AI Cost Model (GVN, DCE, LICM, Inlining, FMA)│
 ├──────────────────────────────────────────────────────────────────────────────────────────┤
 │  LAYER 5: Polyhedral Loop Nest Transformation Engine (Iteration Tiling, Skewing, Fusion) │
 ├──────────────────────────────────────────────────────────────────────────────────────────┤
 │  LAYER 6: MIR Tree-Munch Instruction Selection (Canonical Pattern Matching & Costs)      │
 ├──────────────────────────────────────────────────────────────────────────────────────────┤
 │  LAYER 7: Universal Register Allocation (Linear-Scan & Graph-Coloring Spill Resolver)   │
 ├──────────────────────────────────────────────────────────────────────────────────────────┤
 │  LAYER 8: Machine IR (MIR) & Multi-Target Peephole Optimizer                             │
 ├──────────────────────────────────────────────────────────────────────────────────────────┤
 │  LAYER 9: 9-ISA Native Machine Codegen (x86-64, ARM64, RISC-V 64, WASM, PTX, ROCm, etc) │
 ├──────────────────────────────────────────────────────────────────────────────────────────┤
 │  LAYER 10: Sovereign Binary Object Writers (PE/COFF, ELF64, Mach-O 64, WebAssembly)      │
 ├──────────────────────────────────────────────────────────────────────────────────────────┤
 │  LAYER 11: Sovereign Linkers & Toolchain Suite (PE Linker, Archive Tool, Disassembler)   │
 ├──────────────────────────────────────────────────────────────────────────────────────────┤
 │  LAYER 12: In-Memory JIT Execution Engine (W^X Virtual Memory, Hardware SEH/VEH Traps)   │
 └─────────────────────────────────────────────┬────────────────────────────────────────────┘
                                               │
                                   [ Execution / Machine Code ]
```

---

## 3. Cross-Platform Library Support Matrix

DGTL builds native static archives and dynamic shared libraries for all major platforms:

| Operating System | Target Triple | Dynamic / Shared Library | Import Library | Static Archive |
| :--- | :--- | :--- | :--- | :--- |
| **Windows** | `x86_64-pc-windows-gnu` / `msvc` | `dgtl.dll` | `libdgtl.dll.a` & `dgtl_dll.lib` | `dgtl.lib` & `libdgtl.a` |
| **Linux (x64)** | `x86_64-unknown-linux-gnu` | `libdgtl.so` (`libdgtl.so.2.1.0`) | Direct ELF Dynamic Link | `libdgtl.a` |
| **Linux (ARM)** | `aarch64-unknown-linux-gnu` | `libdgtl.so` (`libdgtl.so.2.1.0`) | Direct ELF Dynamic Link | `libdgtl.a` |
| **macOS (Apple Silicon)**| `aarch64-apple-darwin` | `libdgtl.dylib` | Direct Mach-O Dynamic Link | `libdgtl.a` |
| **macOS (Intel)** | `x86_64-apple-darwin` | `libdgtl.dylib` | Direct Mach-O Dynamic Link | `libdgtl.a` |

---

## 4. Multi-Target Hardware Backend Coverage (9 ISAs)

DGTL delivers instruction selection and machine code generation across 9 discrete targets with 100% feature parity:

| Target Architecture | Dialect / Spec | Vectorization / SIMD | Calling Conventions |
| :--- | :--- | :--- | :--- |
| **x86-64 / AMD64** | AVX-512, AVX2, SSE4.2 | 128 / 256 / 512-bit SIMD | Microsoft x64, System V AMD64 ABI |
| **ARM64 / AArch64** | ARMv8.5-A, ARMv9-A | NEON, SVE, SVE2 | AAPCS64 |
| **RISC-V 64** | RV64GC, RV64IMAFD | RVV Vector Extension | Standard LP64D |
| **WebAssembly** | WASM MVP + SIMD128 | 128-bit SIMD vector ops | WASI C-ABI |
| **NVIDIA CUDA** | PTX ISA 7.0+ | Warp-level primitives | PTX Parameter passing |
| **AMD ROCm** | GCN / CDNA / RDNA | Wavefront SIMD instructions | AMDGPU kernel ABI |
| **SPIR-V** | Vulkan / OpenCL compute | Subgroup vector arithmetic | SPIR-V logical addressing |
| **NASM / Netwide** | Intel syntax assembly | Complete x86 mnemonic set | Universal x64 ABI |
| **ARM32 / Thumb-2** | ARMv7-A | NEON 128-bit SIMD | AAPCS32 |

---

## 5. Sovereign Zero-Dependency Build Guide

DGTL requires no third-party build systems. You compile the sovereign C++ build orchestrator using your C++20 compiler, which then manages all builds, tests, and packaging.

### Step 1: Bootstrap the Sovereign Build Orchestrator
```bash
# On Windows (MinGW-w64 / MSYS2 UCRT64 / GCC):
g++ -std=c++20 -O2 -pipe tools/cpp/dgtl_main.cpp -o dgtl.exe

# On Linux / POSIX:
g++ -std=c++20 -O2 -pipe tools/cpp/dgtl_main.cpp -o dgtl

# On macOS:
clang++ -std=c++20 -O2 tools/cpp/dgtl_main.cpp -o dgtl
```

### Step 2: Build DGTL Libraries
```bash
# Build BOTH static archives (.lib/.a) and dynamic shared libraries (.dll/.so/.dylib):
./dgtl build-all-libs

# Or build specific library formats:
./dgtl build-static    # Produces dgtl.lib and libdgtl.a
./dgtl build-shared    # Produces dgtl.dll (+ libdgtl.dll.a) or libdgtl.so / libdgtl.dylib
```

### Step 3: Build the Complete Sovereign Toolchain Suite
```bash
./dgtl build-tools
```

### Step 4: Full Sovereign Production Release
```bash
# Clean, generate code, validate, compile all modules, libraries, and package with verification:
./dgtl production
```

### Step 5: Package Distribution SDK
```bash
./dgtl package --verify
```

---

## 6. Sovereign Package Manifests (`dgtl.pkg` & `dgtl.toml`)

DGTL natively adheres to the **MYZON Sovereign Package Standard**, allowing seamless consumption without CMake or external package managers:

```toml
[package]
name = "dgtl"
version = "2.1.0"
edition = "2026"
license = "Proprietary-Sovereign"
description = "Deep General-purpose Transformation Layer Compiler & JIT Engine"

[lib]
name = "dgtl"
crate-type = ["staticlib", "cdylib"]

[target.x86_64-pc-windows-gnu]
static-lib = "lib/x64/dgtl.lib"
static-lib-alt = "lib/x64/libdgtl.a"
shared-lib = "bin/dgtl.dll"
import-lib = "lib/x64/libdgtl.dll.a"
include = "include"

[target.x86_64-unknown-linux-gnu]
static-lib = "lib/x64/libdgtl.a"
shared-lib = "lib/x64/libdgtl.so"
include = "include"

[target.aarch64-unknown-linux-gnu]
static-lib = "lib/arm64/libdgtl.a"
shared-lib = "lib/arm64/libdgtl.so"
include = "include"

[target.x86_64-apple-darwin]
static-lib = "lib/libdgtl.a"
shared-lib = "lib/libdgtl.dylib"
include = "include"

[target.aarch64-apple-darwin]
static-lib = "lib/libdgtl.a"
shared-lib = "lib/libdgtl.dylib"
include = "include"

[dependencies]
# Zero third-party runtime dependencies (100% sovereign)
```

---

## 7. Industrial 12-Tier Verification Test Suite (`NEWTEST`)

DGTL includes an enterprise-grade automated test runner ([`newtest/run_newtest.ps1`](file:///D:/TataG/DGTL/newtest/run_newtest.ps1)) covering 54 standalone test modules:

```powershell
# Run the complete test suite (Golden Diagnostic Oracle + Master Test Suite):
powershell.exe -ExecutionPolicy Bypass -File .\newtest\run_newtest.ps1

# Run only the 12-layer full stack diagnostic oracle:
.\newtest\run_newtest.ps1 -Target oracle

# Test against the dynamic shared library (dgtl.dll):
.\newtest\run_newtest.ps1 -Target oracle -LinkType shared

# Run an isolated architectural tier:
.\newtest\run_newtest.ps1 -Target tier7    # Concurrency Torture (16 threads)
.\newtest\run_newtest.ps1 -Target tier11   # Toolchain Suite (14 tools)

# Export standard JUnit 3.0 XML for CI/CD pipelines:
.\newtest\run_newtest.ps1 -Target all -JUnitXml build\newtest\test-results.xml

# List all 54 available tests and architectural tiers without building:
.\newtest\run_newtest.ps1 -List
```

---

## 8. C and C++20 SDK Integration

### C++20 Zero-Overhead Modern SDK Usage:
```cpp
#include <dgtl_sdk/dgtl.h>
#include <iostream>

int main() {
    // 1. Initialize DGTL compilation context
    dgtl::Context ctx;

    // 2. Create transactional IR stream
    dgtl::IRStream ir(ctx);
    auto fn = ir.create_function("compute_sum", dgtl::Type::I32, {dgtl::Type::I32, dgtl::Type::I32});

    auto arg0 = fn.get_param(0);
    auto arg1 = fn.get_param(1);
    auto sum  = ir.build_add(arg0, arg1);
    ir.build_return(sum);

    // 3. Execute optimization pipeline (all 26 passes)
    dgtl::PassManager pm;
    pm.add_default_pipeline(dgtl::OptLevel::O2);
    pm.run(fn);

    // 4. JIT compile to native machine code & execute in-memory
    dgtl::JITEngine jit(ctx);
    auto native_fn = jit.compile_function<int(int, int)>(fn);

    int result = native_fn(40, 2);
    std::cout << "Computed JIT Result: " << result << std::endl; // 42
    return 0;
}
```

### Compiling Client Code Against DGTL:
```bash
# Static linking (Windows):
g++ -std=c++20 -O2 client.cpp dgtl.lib -lkernel32 -lntdll -o client.exe

# Dynamic shared linking (Windows):
g++ -std=c++20 -O2 -DDGTL_IMPORT_SHARED client.cpp libdgtl.dll.a -o client.exe

# Static linking (Linux):
g++ -std=c++20 -O2 client.cpp libdgtl.a -lpthread -ldl -o client

# Dynamic shared linking (Linux):
g++ -std=c++20 -O2 client.cpp -L. -ldgtl -o client
```

---

## 9. Sovereign Toolchain Executables Reference

The DGTL unified multi-call dispatcher (`dgtl-tools.exe` / `dgtl_release.exe`) hosts 14 production utilities:

| Executable | Subcommand | Purpose |
| :--- | :--- | :--- |
| `dgtl-opt.exe` | `dgtl opt` | Intermediate Representation optimizer running 26 discrete transform passes |
| `dgtl-llc.exe` | `dgtl llc` | Multi-target machine code compiler emitting native assembly and binary code |
| `dgtl-link.exe` | `dgtl link` | Sovereign PE/COFF, ELF, and Mach-O standalone binary linker |
| `dgtl-ar.exe` | `dgtl ar` | Sovereign deterministic archiver creating `.lib` and `.a` libraries |
| `dgtl-as.exe` | `dgtl as` | Native assembler encoding mnemonics into relocatable object files |
| `dgtl-objdump.exe` | `dgtl objdump` | Disassembler and forensic binary section dumper |
| `dgtl-inspect.exe` | `dgtl inspect` | Binary security forensics, W^X compliance, relocations, and SEH unwinding |
| `dgtl-verify.exe` | `dgtl verify` | Formal SMT-LIB2 bit-vector equivalence and theorem verifier |
| `dgtl-engine.exe` | `dgtl engine` | Ultra-low-latency in-memory JIT execution runner |
| `dgtl-filecheck.exe`| `dgtl filecheck` | Industrial test pattern matching utility (`CHECK`, `CHECK-NEXT`, `CHECK-NOT`) |
| `dgtl-rodata.exe` | `dgtl rodata` | Read-only data string interning generator using 32-bit FNV-1a hashing |
| `dgtl-debugger.exe`| `dgtl debugger` | Process telemetry, crash diagnostics, and stack frame inspection |
| `dgtl-config.exe` | `dgtl config` | ABI version query, target ISA discovery, and compiler configuration |

---

## 10. Repository Directory Structure

```
DGTL/
├── .github/workflows/          # Industrial CI/CD workflows for Windows, Linux, macOS
├── build/                      # Build outputs, object cache, and staged SDK distributions
│   ├── DGTL_SDK/               # Authoritative public SDK (headers, libraries, manifests)
│   │   ├── bin/                # Executables & dynamic shared libraries (dgtl.dll)
│   │   ├── include/            # Clean namespaced public SDK headers (dgtl_sdk/)
│   │   └── lib/x64/            # Static archives (dgtl.lib, libdgtl.a) & import libs
│   └── package/                # Production distribution release staging
├── docs/                       # Architectural manuals and technical documentation
├── include/
│   ├── dgtl/                   # Modular internal C++20 module partitions
│   │   ├── analysis/           # AI cost model, polyhedral SCoP, and liveness analysis
│   │   ├── api/                # High-level AOT, JIT, and Hybrid session interfaces
│   │   ├── core/               # TargetMachine, transactional memory arena, CFG graphs
│   │   ├── ir/                 # Canonical IRNode, IRStream, and SSA form representations
│   │   ├── isa/                # Native instruction emitters across all 9 ISAs
│   │   ├── jit/                # In-memory W^X JIT execution engine
│   │   ├── linker/             # Sovereign PE, ELF, Mach-O, and WASM binary linkers
│   │   ├── mir/                # Machine IR pipeline, tree-munch ISel, and regalloc
│   │   ├── object/             # Object file readers and binary object writers
│   │   └── pass/               # 26 optimization passes across 11 stages
│   └── dgtl_sdk/               # Public SDK headers (dgtl.h, types.h, math.h, client_debug.h)
├── newtest/                    # 12-Tier Sovereign Industry-Grade Verification Test Suite
│   ├── run_newtest.ps1         # Industrial test runner harness with JUnit 3.0 reporting
│   ├── test_client_debug_oracle.cpp # 12-layer diagnostic oracle audit
│   └── tier1_* .. tier12_*     # 54 exhaustive standalone test modules
├── src/                        # Core implementation source files & app entry points
│   ├── apps/                   # Standalone tool entry points (dgtl-opt, dgtl-link, etc.)
│   ├── main.cpp                # Release binary driver and multi-call forwarder
│   └── sdk/                    # Public C-ABI implementation
├── tools/                      # Tool implementations & Sovereign Build Orchestrator
│   └── cpp/dgtl_main.cpp       # Zero-dependency build orchestrator
├── dgtl.pkg                    # MYZON-compatible sovereign package manifest
├── dgtl.toml                   # MYZON-compatible sovereign package configuration
├── LICENSE                     # Proprietary sovereign software license notice
└── README.md                   # Authoritative repository documentation
```

---

## 11. Proprietary Sovereign License

Copyright (c) 2026 **TataG Systems**. All rights reserved.

Project Owner: **Divyanshu Tata**  
Repository: [github.com/TataG-Systems/DGTL](https://github.com/TataG-Systems/DGTL)

This software is the proprietary and confidential property of TataG Systems. Unauthorized reproduction, reverse engineering, decompilation, or distribution of this software, or any portion thereof, without explicit written consent from TataG Systems is strictly prohibited.
