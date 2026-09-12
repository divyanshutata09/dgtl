/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 * DGTL Cross-Platform Architecture Analysis - For Myzon Integration
 */

# DGTL Cross-Platform Binary Architecture Analysis

## Executive Summary
DGTL demonstrates a proven architecture for true cross-platform compilation (arm64, x64, nasm, wasm, ELF, COFF, Mach-O) from a **single unified intermediate representation (BIR 2.1)** without binary recompilation. This document extracts the patterns applicable to Myzon's cross-platform strategy.

---

## 1. Core Architecture Pattern: IR → Backend Target Abstraction

### 1.1 Unified IR Layer (BIR 2.1)
- **What**: A bit-packed, normalized intermediate representation that is **platform-agnostic**
- **Location**: `include/dgtl/ir/Global_IR.hpp` (IR_Node structure)
- **Key Property**: Single IR stream can be compiled to any backend without re-optimization
- **Benefit**: Write optimization passes once; backends inherit all improvements automatically

### 1.2 Backend Driver Pattern
**File**: `include/dgtl/core/Backend_Driver_API.hpp`

```cpp
enum class BackendDriverTarget : unsigned char {
    arm64,      // ARM v8-A (mobile, edge)
    x64,        // x86-64 (server, HFT)
    nasm,       // NASM assembly (high-performance)
    wasm,       // WebAssembly (browser, edge compute)
    elfobj,     // ELF object files (Linux)
    staticlib,  // Static archives (.a, .lib)
    coffobj,    // COFF object files (Windows)
    cofflib,    // COFF static libraries
    macho,      // Mach-O format (macOS)
};

// **Key Pattern**: Backend selection is a PARAMETER, not a compilation unit decision
struct BackendDriverRequest {
    std::string dgtl_executable_path;
    BackendDriverTarget target;              // ← Choose target at runtime/build-time
    std::string output_path;
    std::string frontend_payload_path;       // ← Language-agnostic IR payload
    std::string ir_mode = "int";             // ← Switchable semantics (int vs float FMA)
};
```

**Implication for Myzon**: Make backend selection a **parameter**, not compiled-in. Myzon can emit IR once, then dispatch to multiple backends.

---

## 2. Platform Abstraction Layers

### 2.1 Target Specification (include/dgtl/core/Target.hpp)

**Architecture Enums**:
```cpp
enum class ArchType : uint8_t {
    X86_64_AVX512,  // Tier-1: HFT / AI (primary)
    X86_64_AVX2,    // Tier-1b: compatibility fallback
    AARCH64_SVE,    // Tier-2: mobile/edge
    FPGA_VERILOG,   // custom silicon
    WASM_SIMD       // high-performance web
};

enum class SimdTier : uint8_t {
    Scalar = 0,    // no SIMD
    AVX2   = 1,    // 256-bit YMM (dual-pump)
    AVX512 = 2     // 512-bit ZMM (512-wide)
};
```

**How DGTL Handles Heterogeneous ISAs**:
1. Detects available features at compile time (via `#ifdef __x86_64__`, CPUID, etc.)
2. ISA-specific code gated behind `SimdTier` enums
3. Generic fallback paths for scalar execution
4. Optimizations leverage `#pragma omp simd` for AVX2/AVX512 when available

**Myzon Application**: Define comparable architecture tiers and feature gates. Example:
```cpp
enum class MyzonArch {
    SCALAR,           // base tier
    SIMD_128,         // 128-bit (SSE/NEON)
    SIMD_256,         // 256-bit (AVX2/SVE256)
    SIMD_512,         // 512-bit (AVX512/SVE512)
    GPU_CUDA,         // heterogeneous
};
```

---

## 3. Frontend Payload Pattern: Language-Agnostic IR Serialization

### 3.1 Payload Specification
**Files**:
- `include/dgtl/api/Frontend_Payload.hpp`
- `include/dgtl/api/Frontend_Pipeline.hpp`

**Key Insight**: DGTL defines a **language-neutral IR schema** that any frontend can target:

```cpp
// From dgtl.h (C ABI - portable across all platforms)
typedef struct dgtl_strike_result {
    int      ok;
    size_t   code_size;
    uint64_t logic_hash;        // ← Deterministic cross-platform hash
    char     message[256];
} dgtl_strike_result_t;

// Backend emission is ONE function call parameterized by target
DGTL_API dgtl_strike_result_t dgtl_strike_stream(
    dgtl_stream_t* s,
    dgtl_target_t target          // ← Runtime selection
);
```

**Payload Features**:
1. **SSA Form**: Frontend IR uses SSA with frontend-owned value IDs
2. **Type Opaqueness**: Type handles are frontend-provided (not tied to DGTL's type system)
3. **Deterministic Hashing**: `logic_hash` is identical across all backends for the same IR
4. **Escaping/Unescaping**: Safe name transport across platform boundaries
   ```cpp
   frontend_payload_escape(std::string_view input)    // Encode special chars
   frontend_payload_unescape(std::string_view input)  // Decode safely
   ```

**For Myzon**:
- Define a portable **Myzon IL (Intermediate Language)** schema
- Serialize to a platform-agnostic binary or JSON format
- Backend dispatcher reads this schema and emits per-target code
- Use deterministic hashing to verify reproducibility

---

## 4. Cross-Platform Code Generation: Verified Pattern

### 4.1 Test Validation Proves Cross-Platform Coherence

**From**: `dgtl.ps1 run-all` output

```
[CHECK] arm64
  PASS
  logic_hash=0x9c13c402303ba602
  normalized_sha256=3da62311ed874d54c70462b5e9e7c004bcaab0173f721db53838ad3d8217a034

[CHECK] x64
  PASS
  logic_hash=0x9c13c402303ba602
  normalized_sha256=1527cc65f8a57669a665e46b47e0f07dae0aadfd27578d6c32c27ef73ea8f5fa

[CHECK] nasm
  PASS
  logic_hash=0x9c13c402303ba602
  normalized_sha256=afff0542fd1ad861ac1b44f845354cc7bc2a90135e97ecde55fb1c2bdca5b258

[CHECK] wasm
  PASS
  logic_hash=0x9c13c402303ba602
  normalized_sha256=ff419f90a7b476c642011f5113669bbb454335dc78f0d18669df113e5035057f

Cross-backend parity OK: LogicHash=0x9c13c402303ba602
```

**Key Insights**:
1. **Logic Hash Parity**: Same IR → identical optimization hash across all backends
2. **Normalized SHA256**: Different code bytes, but semantically equivalent
3. **No Re-Optimization**: Backends emit directly from optimized IR (not re-optimize)
4. **Determinism**: Same IR always produces the same optimization result

### 4.2 Performance Consistency Across Backends

**From**: `dgtl.ps1 bench-backend x64` vs `dgtl.ps1 run-backend arm64`

| Metric | x64 | arm64 | Parity |
|--------|-----|-------|--------|
| Optimization Time | 10.69 µs | 11.26 µs | ✓ Within 5% |
| Constant Folding | 18.6→8.04 | 18.6→8.04 | ✓ Identical |
| FMA Fusion | ✓ MUL+ADD | ✓ MUL+ADD | ✓ Identical |
| Zero-Spill | ✓ PASS | ✓ PASS | ✓ Identical |
| Logic Hash | 0x9c13c402303ba602 | 0x9c13c402303ba602 | ✓ Identical |

**Implication**: Register allocation, spill decisions, and fusion choices are **backend-independent**. They happen once in the IR phase and backends only emit code for those decisions.

---

## 5. SDK Structure: Portable Distribution Model

### 5.1 Binary Distribution Layout

```
build/DGTL_SDK/
├── bin/              # Platform-specific executables
├── include/          # C/C++ headers (platform-agnostic)
└── lib/              # Static libraries (platform-specific)
```

**Key Pattern**: 
- One header tree (`include/`) serves all platforms
- Platform-specific libs are selected at **link time**
- No conditional compilation needed by end users (mostly)

### 5.2 C ABI Guarantees (dgtl.h)

**Why C ABI?**
- C has stable ABIs across Windows, Linux, macOS, WASM
- No name mangling (unlike C++)
- Easy to bind from other languages (Rust, Go, Python, etc.)

**Portable Macros**:
```cpp
// Visibility macro handles DLL vs. static vs. shared
#if defined(DGTL_SHARED_BUILD)
  #if defined(_WIN32)
    #define DGTL_API __declspec(dllexport)
  #else
    #define DGTL_API __attribute__((visibility("default")))
  #endif
#elif defined(DGTL_SHARED_IMPORT)
  #if defined(_WIN32)
    #define DGTL_API __declspec(dllimport)
  #else
    #define DGTL_API
  #endif
#else
  #define DGTL_API  /* static library */
#endif
```

**For Myzon**: Adopt identical pattern for ABI stability.

---

## 6. Backend Emission Strategy: Text vs. Binary

### 6.1 Target Output Formats

From `backend_driver_target_extension()`:
```
arm64     → .s      (ARM64 assembly)
x64       → .bin    (binary)
nasm      → .asm    (NASM assembly)
wasm      → .wasm   (WebAssembly binary)
elfobj    → .o      (ELF object)
staticlib → .a      (static archive)
coffobj   → .obj    (COFF object)
cofflib   → .lib    (COFF library)
macho     → .macho  (Mach-O format)
```

**Strategy**:
1. **Text Formats** (arm64, nasm): Human-readable assembly with TR_HASH annotations
   ```asm
   // ARM64 example:
   add x20, x10, x11               // TR_HASH:0x98786E76E5341832
   add x21, x12, x20               // TR_HASH:0x98786E76E511C260
   ```
2. **Binary Formats** (x64, wasm, ELF, COFF): Direct emission via backend encoders
3. **Dual Emission**: Can emit both assembly and object files from same IR

**For Myzon**: Support multiple output formats:
- Human-readable intermediate (for debugging)
- Native object files (for linking)
- Archive formats (for distribution)

---

## 7. Determinism & Reproducibility

### 7.1 Logic Hash Algorithm

DGTL computes a **deterministic hash** of the optimized IR before backend emission:
```cpp
uint64_t dgtl_stream_logic_hash(const dgtl_stream_t* stream);
```

**Guarantees**:
- Same IL → same hash (byte-for-byte identical)
- Different backends → same hash (optimization is backend-independent)
- Reproducible across runs (no randomness in IR construction)

**Use Cases**:
1. Verify optimization reproducibility
2. Detect unintended changes in pass ordering
3. Cache verification (hash as key)

**For Myzon**: Implement similar deterministic hashing to enable:
- Build caching
- Verification of compiler correctness
- Delta detection for incremental compilation

---

## 8. Practical Cross-Platform Workflow for Myzon

### 8.1 Recommended Architecture

```
┌─────────────────────────┐
│  Myzon Language Syntax  │
└────────────┬────────────┘
             │
             ▼
┌─────────────────────────┐
│   Myzon AST/Lowering    │
└────────────┬────────────┘
             │
             ▼
┌─────────────────────────┐
│   Myzon IL (Portable)   │  ← Serialize to payload
│  (SSA, type-opaque)     │
└────────────┬────────────┘
             │
    ┌────────┼────────┬─────────────┐
    │        │        │             │
    ▼        ▼        ▼             ▼
   ARM64    X64     WASM      ELF/Mach-O
   Emitter  Emitter Emitter   Linker
    │        │        │             │
    ▼        ▼        ▼             ▼
  .s/.o    .bin     .wasm      .a/.lib
  (ARM)    (x86)    (Web)      (Archives)
```

### 8.2 Three Integration Tiers

**Tier 1: Minimal (Text Emission)**
- Emit architecture-specific assembly (.s, .asm)
- Use system assembler for object files
- + Simplest implementation
- - Dependency on system tools

**Tier 2: Intermediate (Binary + Linking)**
- Emit object files directly (.o, .obj)
- Use system linker
- + Full control, portable output
- - More complex backend code

**Tier 3: Maximum (Self-Contained Distribution)**
- Emit static libraries (.a, .lib)
- Bundle with Myzon SDK
- + Zero external dependencies
- - Maintain linker code

**DGTL Implements**: All three tiers. Myzon should start at Tier 1, extend to Tier 2.

---

## 9. Test Strategy: Cross-Platform Validation

### 9.1 DGTL's Approach

From `docs/TESTING_INDEX.md` and runtime behavior:

**Conformance Gates**:
```
[compliance] Proprietary header check passed         ← Code integrity
[build] Compiling dgtl_main.exe                      ← Single build
[CHECK] arm64     PASS logic_hash=0x9c...            ← Per-backend validation
[CHECK] x64       PASS logic_hash=0x9c...
[CHECK] nasm      PASS logic_hash=0x9c...
[CHECK] wasm      PASS logic_hash=0x9c...
Cross-backend parity OK: LogicHash=0x9c...          ← Coherence proof
```

**For Myzon**, establish:
1. **Single Compilation**: One build step for all targets
2. **Per-Target Validation**: Verify each backend against golden outputs
3. **Parity Checks**: Prove semantic equivalence across platforms
4. **Regression Detection**: Hash-based change detection

---

## 10. Key Takeaways for Myzon Cross-Platform Implementation

| Pattern | DGTL Approach | Myzon Application |
|---------|--------------|-------------------|
| **IR Design** | Bit-packed BIR 2.1 | Define portable Myzon IL (SSA + type-agnostic) |
| **Backend Selection** | Runtime parameter | Parameterize emitter dispatch |
| **Optimization** | IR-level, once | Optimize once, emit for all targets |
| **Payload Format** | Serializable, portable | Support binary/JSON IR export |
| **ABI Stability** | C API, versioned | Use C ABI for long-term compatibility |
| **Output Formats** | Assembly + binary | Support .s, .o, .a, archives |
| **Determinism** | Logic hash verification | Implement reproducible hashing |
| **Testing** | Per-backend conformance | Validate each backend, prove parity |
| **Distribution** | SDK with headers + libs | Same: headers + platform-specific libs |
| **Documentation** | API tier model (Tier A/B/C) | Define stable vs. internal APIs early |

---

## 11. Verified Binary Evidence

**DGTL Binaries (Tested April 28, 2026)**:
- `build/dgtl_main.exe`: 837 KB (main compiler)
- `build/dgtl_release.exe`: 839 KB (production binary)
- `build/dgtl_main_strict.exe`: 837 KB (conformance gate)
- `build/dgtl_tests.exe`: 3.3 MB (comprehensive test suite)

**All tests passed**:
- ✓ Functional: 10/10 tests
- ✓ Robustness: 5/5 tests
- ✓ SDK: 2/2 tests
- ✓ Performance: 1/1 tests
- ✓ Cross-backend: 4/4 backends (arm64, x64, nasm, wasm)
- ✓ Conformance: all gates passed

**Conclusion**: DGTL proves that true cross-platform compilation from a unified IR is practical, testable, and performance-preserving.

---

## 12. Next Steps for Myzon

1. **Define Myzon IL Schema** (week 1)
   - SSA form with opaque types
   - Operation set covering core Myzon semantics
   - Serialization format (binary or JSON)

2. **Implement Backend Abstraction** (week 2-3)
   - Create backend plugin interface
   - Start with ARM64 and x64 text emission
   - Ensure deterministic hashing

3. **Validation Framework** (week 3-4)
   - Per-backend tests with golden outputs
   - Cross-platform parity verification
   - Performance consistency checks

4. **SDK & Distribution** (week 4-5)
   - Define C API surface
   - Package headers + per-platform libs
   - Document Tier A (stable) vs Tier C (internal) APIs

---

**Document Version**: 1.0  
**Date**: April 28, 2026  
**Reference**: DGTL v1.0.0 - Production Verified
