# DGTL Developer Experience Specification

**Deep General-purpose Transformation Layer — SDK Guide for Language Creators**

> **Version:** 1.0.0 · **Author:** Divyanshu Tata / TataG Systems · **Date:** 2026-04-20
>
> Zero-dependency, high-throughput IR transformation and backend emission engine
> for HFT systems, performance-critical runtimes, and programming languages.

---

## Table of Contents

1. [Quick-Start: Your First Strike in 5 Minutes](#1-quick-start-your-first-strike-in-5-minutes)
2. [SDK Layout & Distribution](#2-sdk-layout--distribution)
3. [Architecture & IR Philosophy](#3-architecture--ir-philosophy)
4. [Complete API Reference](#4-complete-api-reference)
5. [Building a Language on DGTL — Step by Step](#5-building-a-language-on-dgtl--step-by-step)
6. [The MYZON Pattern — Production Reference](#6-the-myzon-pattern--production-reference)
7. [Optimization Pipeline](#7-optimization-pipeline)
8. [Backend Emission & Multi-Targeting](#8-backend-emission--multi-targeting)
9. [SIMD & HFT Stride Alignment](#9-simd--hft-stride-alignment)
10. [Advanced Integration: Custom Dialects & ](#10-advanced-integration-custom-dialects--)
11. [All Compiler & SDK Flags](#11-all-compiler--sdk-flags)
12. [Error Codes & Troubleshooting](#12-error-codes--troubleshooting)
13. [Frontend Conformance Kit](#13-frontend-conformance-kit)
14. [Performance Tuning Checklist](#14-performance-tuning-checklist)
15. [Licensing & Compliance](#15-licensing--compliance)

---

## 1. Quick-Start: Your First Strike in 5 Minutes

### Hello World — C API (15 lines)

```c
#include "dgtl.h"

int main(void) {
    /* 1. Create execution context */
    dgtl_context_t* ctx = dgtl_context_create();

    /* 2. Create an IR stream (reserve 256 node slots) */
    dgtl_stream_t* s = dgtl_stream_create(ctx, 256);

    /* 3. Build IR: return 100 + 200 */
    uint32_t c1 = dgtl_emit_const_i64(s, 100);
    uint32_t c2 = dgtl_emit_const_i64(s, 200);
    uint32_t r  = dgtl_emit_add_i32(s, c1, c2);
    dgtl_emit_return(s, r);

    /* 4. Optimize */
    dgtl_optimize(s);

    /* 5. Strike (compile) to x64 binary */
    dgtl_strike_result_t res = dgtl_strike_stream(s, DGTL_TARGET_X64);
    /* res.ok == 1, res.code_size > 0, res.logic_hash is deterministic */

    /* 6. Clean up */
    dgtl_stream_destroy(s);
    dgtl_context_destroy(ctx);
    return res.ok ? 0 : 1;
}
```

### Hello World — FMA (Fused Multiply-Add)

```c
#include "dgtl.h"
#include "dgtl_simd.h"

int main(void) {
    dgtl_context_t* ctx = dgtl_context_create();
    dgtl_stream_t*  s   = dgtl_stream_create(ctx, 256);

    /* FMA: result = (A * B) + C */
    uint32_t a = dgtl_emit_const_i64(s, 10);
    uint32_t b = dgtl_emit_const_i64(s, 20);
    uint32_t c = dgtl_emit_const_i64(s, 5);
    uint32_t r = dgtl_emit_fma(s, a, b, c);   /* = 205 */
    dgtl_emit_return(s, r);

    dgtl_strike_result_t res = dgtl_strike_stream(s, DGTL_TARGET_ARM64);

    dgtl_stream_destroy(s);
    dgtl_context_destroy(ctx);
    return 0;
}
```

### Linking

**Windows (MSVC):**
```
cl.exe /std:c17 /I path\to\DGTL_SDK\include main.c path\to\DGTL_SDK\lib\dgtl.lib /Fe:my_compiler.exe
```

**Windows (GCC/UCRT64 — recommended):**
```bash
g++ -std=c++26 -O2 -I path/to/DGTL_SDK/include main.cpp -L path/to/DGTL_SDK/lib -ldgtl -o my_compiler.exe
```

**Linux (GCC/Clang):**
```bash
g++ -std=c++26 -O2 -I path/to/DGTL_SDK/include main.cpp -L path/to/DGTL_SDK/lib -ldgtl -o my_compiler
# or
clang++ -std=c++26 -O2 -I path/to/DGTL_SDK/include main.cpp -L path/to/DGTL_SDK/lib -ldgtl -o my_compiler
```

---

## 2. SDK Layout & Distribution

```
DGTL_SDK/
├── include/
│   ├── dgtl.h            # Main entry — all you need for basic usage
│   ├── dgtl_core.h       # Extended IR opcodes, flags, optimization profiles
│   └── dgtl_simd.h       # SIMD/FMA helpers, gather/scatter, legalizer
├── lib/
│   ├── dgtl.lib          # Windows static library
│   └── libdgtl.a         # Linux/macOS static library
├── bin/
│   └── dgtl_release.exe  # CLI backend driver (for out-of-process emission)
└── LICENSE
```

### Two Integration Modes

| Mode | Description | Latency | Use Case |
|------|-------------|---------|----------|
| **In-Process (Static Link)** | Link `dgtl.lib` directly into your compiler binary | ~0 IPC overhead | HFT, , tight integration |
| **Out-of-Process (CLI)** | Write `.dgtl.payload` file, invoke `dgtl_release.exe` | Process spawn cost | Loose coupling, web pipelines, CI |

---

## 3. Architecture & IR Philosophy

### The Four Pillars

```
┌──────────────┐    ┌──────────────┐    ┌──────────────┐    ┌──────────────┐
│   IR Layer   │───▶│  Graph Layer │───▶│  Pass Layer  │───▶│ Backend/  │
│              │    │              │    │              │    │              │
│ Bit-packed   │    │ Execution    │    │ GVN, DCE,    │    │ x64, ARM64,  │
│ atoms+cores  │    │ graph with   │    │ SIMD, FMA    │    │ WASM, NASM,  │
│ 32-byte nodes│    │ regions &    │    │ fusion,      │    │ ELF, COFF,   │
│ Shadow buffer│    │ SCoP detect  │    │ speculation  │    │ Mach-O       │
└──────────────┘    └──────────────┘    └──────────────┘    └──────────────┘
```

### Transactional IR (Shadow Buffer)

DGTL's IR stream supports **speculative optimizations** with safe rollbacks:

```
Transaction Begin
  ├── Modify IR nodes in shadow buffer
  ├── Run optimization pass
  ├── Verify: did the transform improve cost?
  │     YES → Commit (shadow → primary)
  │     NO  → Rollback (discard shadow, restore primary)
Transaction End
```

This enables aggressive transformations (FMA fusion, speculation, predication) without risk of corrupting the IR.

### Memory Layout

- **IRAtom:** 4 bytes (packed operand references)
- **IRCore:** 32 bytes (opcode, flags, operands, hash)
- **IRNode:** 32-byte unified view combining atom + core
- **Cache alignment:** 64-byte boundaries for hot-path structures
- **Arena allocation:** Thread-local, 64 MB default per thread, up to 64 threads

### The LogicHash — Determinism Guarantee

Every IR stream produces a **64-bit LogicHash**. The guarantee:

> **Same input IR + same optimization flags = identical LogicHash = identical binary output.**

```c
uint64_t hash = dgtl_stream_logic_hash(stream);
// 0x9c13c402303ba602 — deterministic across all backends
```

This is critical for HFT compliance audits and reproducible builds.

### HFT Stride Alignment

DGTL's hot path assumes **8-byte stride alignment**. To hit peak throughput (250M+ nodes/sec):

- All data passed to IR emission must be 64-bit aligned
- Use `alignas(8)` on structures passed to DGTL
- Enable `HFT_STRIDE_8` mode for maximum throughput

---

## 4. Complete API Reference

### 4.1 `dgtl.h` — Core API (Tier A: Stable)

#### Version
```c
#define DGTL_VERSION_MAJOR  1
#define DGTL_VERSION_MINOR  1
#define DGTL_VERSION_PATCH  0
#define DGTL_VERSION_STRING "1.0.0"

const char* dgtl_version_string(void);
```

#### Opaque Handles
```c
typedef struct dgtl_context  dgtl_context_t;   /* Global engine context */
typedef struct dgtl_stream   dgtl_stream_t;    /* IR node stream */
typedef struct dgtl_function dgtl_function_t;  /* Function container */
```

#### Context Lifecycle
```c
dgtl_context_t*  dgtl_context_create(void);           /* Create engine context */
void             dgtl_context_destroy(dgtl_context_t* ctx);  /* Destroy + free all */
```

#### Stream Lifecycle
```c
dgtl_stream_t*   dgtl_stream_create(dgtl_context_t* ctx, size_t reserved_nodes);
void             dgtl_stream_destroy(dgtl_stream_t* stream);
size_t           dgtl_stream_node_count(const dgtl_stream_t* stream);
uint64_t         dgtl_stream_logic_hash(const dgtl_stream_t* stream);
```

#### IR Emission — Building the Stream
```c
uint32_t dgtl_emit_const_i64(dgtl_stream_t* s, uint64_t value);       /* Constant */
uint32_t dgtl_emit_add_i32(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);  /* Add */
uint32_t dgtl_emit_sub_i32(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);  /* Sub */
uint32_t dgtl_emit_mul_i32(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);  /* Mul */
uint32_t dgtl_emit_div_i32(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);  /* Div */
uint32_t dgtl_emit_mod_i32(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);  /* Mod */
void     dgtl_emit_return(dgtl_stream_t* s, uint32_t value_slot);          /* Return */
```

Each `dgtl_emit_*` function returns a **slot ID** (uint32_t). Use slot IDs as operands to subsequent instructions. Slots are monotonically increasing within a stream.

#### Optimization
```c
dgtl_status_t dgtl_optimize(dgtl_stream_t* s);  /* Run default optimization pipeline */
```

#### Backend Strike (Compile)
```c
typedef struct dgtl_strike_result {
    int      ok;            /* 1 = success */
    size_t   code_size;     /* bytes of generated code */
    uint64_t logic_hash;    /* deterministic hash */
    char     message[256];  /* error detail if !ok */
} dgtl_strike_result_t;

dgtl_strike_result_t dgtl_strike_stream(dgtl_stream_t* s, dgtl_target_t target);
```

#### File Emission
```c
dgtl_status_t dgtl_emit_to_file(dgtl_stream_t* s, dgtl_target_t target,
                                 const char* output_path);
```

#### Payload Serialization (For Out-of-Process Mode)
```c
dgtl_status_t dgtl_write_payload(const dgtl_stream_t* s, const char* path);
dgtl_status_t dgtl_read_payload(dgtl_stream_t* s, const char* path);
```

#### Backend Targets
```c
typedef enum dgtl_target {
    DGTL_TARGET_ARM64      = 0,   /* ARM64 assembly (.arm64.s)     */
    DGTL_TARGET_X64        = 1,   /* x86-64 binary  (.x64.bin)     */
    DGTL_TARGET_NASM       = 2,   /* NASM assembly  (.nasm.asm)    */
    DGTL_TARGET_WASM       = 3,   /* WebAssembly    (.wasm)        */
    DGTL_TARGET_ELF_OBJ    = 4,   /* ELF relocatable (.o)          */
    DGTL_TARGET_STATIC_LIB = 5,   /* GNU ar archive (.a)           */
    DGTL_TARGET_COFF_OBJ   = 6,   /* COFF object    (.obj)         */
    DGTL_TARGET_COFF_LIB   = 7,   /* COFF library   (.lib)         */
    DGTL_TARGET_MACHO      = 8,   /* Mach-O 64      (.macho)       */
    DGTL_TARGET_COUNT      = 9
} dgtl_target_t;

const char* dgtl_target_name(dgtl_target_t target);
const char* dgtl_target_extension(dgtl_target_t target);
```

#### Status / Error
```c
typedef struct dgtl_status {
    int  ok;            /* 1 = success, 0 = failure */
    int  error_code;
    char message[256];
} dgtl_status_t;
```

---

### 4.2 `dgtl_core.h` — Extended IR & Optimization (Tier A/B)

#### Full IR OpCode Table

```c
typedef enum dgtl_opcode {
    /* Basics */
    DGTL_OP_NOP       = 0x00,   /* No operation */
    DGTL_OP_CONST     = 0x01,   /* Constant load */

    /* Integer Arithmetic */
    DGTL_OP_ADD       = 0x02,   /* a + b */
    DGTL_OP_SUB       = 0x03,   /* a - b */
    DGTL_OP_MUL       = 0x04,   /* a * b */
    DGTL_OP_DIV       = 0x05,   /* a / b */
    DGTL_OP_MOD       = 0x06,   /* a % b */

    /* Bitwise */
    DGTL_OP_AND       = 0x07,   /* a & b */
    DGTL_OP_OR        = 0x08,   /* a | b */
    DGTL_OP_XOR       = 0x09,   /* a ^ b */
    DGTL_OP_SHL       = 0x0A,   /* a << b */
    DGTL_OP_SHR       = 0x0B,   /* a >> b */

    /* Unary */
    DGTL_OP_NEG       = 0x0C,   /* -a */
    DGTL_OP_NOT       = 0x0D,   /* ~a */

    /* Memory */
    DGTL_OP_LOAD      = 0x10,   /* Load from address */
    DGTL_OP_STORE     = 0x11,   /* Store to address */

    /* Calls & Returns */
    DGTL_OP_CALL      = 0x20,   /* Function call */
    DGTL_OP_RET       = 0x21,   /* Return from function */

    /* Control Flow */
    DGTL_OP_LABEL     = 0x30,   /* Basic block label */
    DGTL_OP_JMP       = 0x31,   /* Unconditional jump */
    DGTL_OP_BR_IF     = 0x32,   /* Conditional branch */

    /* Comparisons */
    DGTL_OP_CMP_EQ    = 0x40,   /* a == b */
    DGTL_OP_CMP_NE    = 0x41,   /* a != b */
    DGTL_OP_CMP_LT    = 0x42,   /* a < b  */
    DGTL_OP_CMP_LE    = 0x43,   /* a <= b */
    DGTL_OP_CMP_GT    = 0x44,   /* a > b  */
    DGTL_OP_CMP_GE    = 0x45,   /* a >= b */

    /* Floating-Point & FMA */
    DGTL_OP_FMA       = 0x50,   /* Fused multiply-add: a*b+c */
    DGTL_OP_FADD      = 0x51,   /* Float add */
    DGTL_OP_FMUL      = 0x52,   /* Float multiply */
    DGTL_OP_FSUB      = 0x53,   /* Float subtract */
    DGTL_OP_FDIV      = 0x54,   /* Float divide */

    /* SSA */
    DGTL_OP_PHI       = 0x60,   /* PHI node for SSA merge */

    /* SIMD */
    DGTL_OP_GATHER    = 0x70,   /* Gather load */
    DGTL_OP_SCATTER   = 0x71,   /* Scatter store */

    DGTL_OP_SENTINEL  = 0xFF    /* End marker */
} dgtl_opcode_t;
```

#### IR Node Flags (16-bit Bitfield)

```c
#define DGTL_FLAG_NONE            0x0000  /* No flags */
#define DGTL_FLAG_IS_CONST        0x0001  /* Node is a constant */
#define DGTL_FLAG_IS_FP           0x0002  /* Floating-point operation */
#define DGTL_FLAG_HAS_SIDE_EFFECT 0x0004  /* Cannot be dead-code eliminated */
#define DGTL_FLAG_VOLATILE        0x0008  /* Memory volatile; no reordering */
#define DGTL_FLAG_DEAD            0x0010  /* Marked dead by DCE */
#define DGTL_FLAG_FMA_FUSED       0x0020  /* Part of fused multiply-add */
#define DGTL_FLAG_SPECULATED      0x0040  /* Speculatively executed */
#define DGTL_FLAG_PREDICATED      0x0080  /* Predicated (conditional) execution */
```

#### Optimization Profiles

```c
typedef enum dgtl_opt_profile {
    DGTL_OPT_PROFILE_DEFAULT     = 0,  /* Balanced: GVN + DCE + folding */
    DGTL_OPT_PROFILE_FOLD        = 1,  /* Aggressive constant folding */
    DGTL_OPT_PROFILE_FUSION      = 2,  /* Instruction fusion (MUL+ADD→FMA) */
    DGTL_OPT_PROFILE_SIMD        = 3,  /* SIMD vectorization and legalization */
    DGTL_OPT_PROFILE_PRESSURE    = 4,  /* Register pressure minimization */
    DGTL_OPT_PROFILE_PREDICATION = 5   /* Branch-to-predication conversion */
} dgtl_opt_profile_t;

dgtl_status_t dgtl_optimize_with_profile(dgtl_stream_t* s, dgtl_opt_profile_t profile);
```

#### Extended Emission — Generic OpCode Emitter

```c
uint32_t dgtl_emit_op(dgtl_stream_t* s,
                       dgtl_opcode_t op,
                       uint32_t lhs,
                       uint32_t rhs,
                       uint64_t immediate,
                       uint16_t flags);
```

This is the **power user** emit function. It can emit any opcode with full flag control:

```c
/* Emit a volatile store */
uint32_t store = dgtl_emit_op(s, DGTL_OP_STORE, addr_slot, value_slot,
                               0, DGTL_FLAG_VOLATILE | DGTL_FLAG_HAS_SIDE_EFFECT);

/* Emit a speculated branch */
uint32_t br = dgtl_emit_op(s, DGTL_OP_BR_IF, cond_slot, 0,
                             label_id, DGTL_FLAG_SPECULATED);
```

#### Node Inspection (Read-Back)

```c
typedef struct dgtl_node_info {
    dgtl_opcode_t opcode;
    uint32_t      slot;
    uint32_t      lhs;
    uint32_t      rhs;
    uint64_t      immediate;
    uint16_t      flags;
} dgtl_node_info_t;

int dgtl_stream_get_node(const dgtl_stream_t* s, size_t index, dgtl_node_info_t* out);
```

Use this to iterate over the IR after optimization and inspect what DGTL produced:

```c
for (size_t i = 0; i < dgtl_stream_node_count(s); ++i) {
    dgtl_node_info_t info;
    if (dgtl_stream_get_node(s, i, &info)) {
        printf("Node %zu: op=0x%02X slot=%u lhs=%u rhs=%u imm=%llu flags=0x%04X\n",
               i, info.opcode, info.slot, info.lhs, info.rhs,
               (unsigned long long)info.immediate, info.flags);
    }
}
```

#### IR Mode Selection

```c
typedef enum dgtl_ir_mode {
    DGTL_IR_MODE_INT   = 0,   /* Integer-only IR (default) */
    DGTL_IR_MODE_FLOAT = 1,   /* Floating-point IR */
    DGTL_IR_MODE_FMA   = 2    /* FMA-enabled IR */
} dgtl_ir_mode_t;

dgtl_status_t dgtl_set_ir_mode(dgtl_stream_t* s, dgtl_ir_mode_t mode);
```

---

### 4.3 `dgtl_simd.h` — SIMD & Vectorization (Tier B)

#### SIMD Target Architectures

```c
typedef enum dgtl_simd_arch {
    DGTL_SIMD_X86_AVX512   = 0,   /* 512-bit, 16 lanes (32-bit) */
    DGTL_SIMD_ARM_NEON     = 1,   /* 128-bit, 4 lanes (32-bit)  */
    DGTL_SIMD_WASM_SIMD128 = 2,   /* 128-bit, 4 lanes (32-bit)  */
    DGTL_SIMD_GENERIC      = 3    /* Scalar fallback              */
} dgtl_simd_arch_t;

#define DGTL_SIMD_MAX_LANES_AVX512  16
#define DGTL_SIMD_MAX_LANES_NEON     4
#define DGTL_SIMD_MAX_LANES_WASM128  4
```

#### SIMD IR Emission

```c
/* FMA: result = (a * b) + c  — auto-sets DGTL_FLAG_FMA_FUSED | DGTL_FLAG_IS_FP */
uint32_t dgtl_emit_fma(dgtl_stream_t* s, uint32_t a, uint32_t b, uint32_t c);

/* Gather load: load from base + index vector */
uint32_t dgtl_emit_gather(dgtl_stream_t* s, uint32_t base_slot,
                            uint32_t index_slot, uint16_t flags);

/* Scatter store: store value to base + index vector */
uint32_t dgtl_emit_scatter(dgtl_stream_t* s, uint32_t base_slot,
                             uint32_t index_slot, uint32_t value_slot,
                             uint16_t flags);
```

#### SIMD Legalization Pass

```c
/* Run 4-node sliding-window vectorization for target arch */
dgtl_status_t dgtl_legalize_simd(dgtl_stream_t* s, dgtl_simd_arch_t arch);
```

#### Convenience Macros

```c
/* Linear address: base + index * stride */
#define DGTL_EMIT_LINEAR_ADDR(stream, base, index, stride) \
    dgtl_emit_add_i32((stream), (base),                    \
        dgtl_emit_mul_i32((stream), (index),               \
            dgtl_emit_const_i64((stream), (uint64_t)(stride))))

/* FMA shorthand */
#define DGTL_EMIT_FMA(stream, a, b, c)  dgtl_emit_fma((stream), (a), (b), (c))
```

---

## 5. Building a Language on DGTL — Step by Step

This section walks through building a **complete programming language** using DGTL as your backend, keeping your language fully separate from DGTL's codebase.

### 5.1 Project Structure

```
MyLang/
├── include/
│   └── mylang/
│       ├── Lexer.hpp
│       ├── Parser.hpp
│       ├── AST.hpp
│       ├── Semantic.hpp
│       ├── MIR.hpp            # Your portable intermediate representation
│       └── DGTLBridge.hpp     # Your DGTL integration layer
├── src/
│   ├── main.cpp
│   ├── Lexer.cpp
│   ├── Parser.cpp
│   ├── Semantic.cpp
│   ├── MIR.cpp
│   └── DGTLBridge.cpp         # Maps your IR → DGTL IR
├── examples/
│   └── hello.mylang
├── build/
│   ├── bin/
│   └── out/                   # Emitted machine code goes here
├── deps/
│   └── DGTL_SDK/              # ← Copy or symlink from DGTL dist
│       ├── include/
│       ├── lib/
│       └── bin/
└── build.ps1
```

### 5.2 Architectural Pattern

```
Source Code (.mylang)
       │
       ▼
   ┌─────────┐
   │  Lexer   │  ← YOUR code (tokenizer)
   └────┬─────┘
        ▼
   ┌─────────┐
   │  Parser  │  ← YOUR code (recursive descent / Pratt)
   └────┬─────┘
        ▼
   ┌─────────┐
   │   AST    │  ← YOUR data structures
   └────┬─────┘
        ▼
   ┌──────────┐
   │ Semantic  │  ← YOUR type checking, name resolution, etc.
   └────┬──────┘
        ▼
   ┌──────────┐
   │   MIR    │  ← YOUR portable IR (string-based operands)
   └────┬──────┘
        ▼
   ┌────────────┐
   │ DGTL Bridge│  ← Thin adapter: YOUR MIR → DGTL FrontendLoweringFunction
   └────┬───────┘
        ▼
   ┌──────────────────┐
   │     DGTL SDK      │  ← Optimization + code generation (NOT your code)
   │  (static library)  │
   └────────┬──────────┘
            ▼
   Machine Code (.x64.bin, .arm64.s, .wasm, .nasm.asm, .o, .obj, .lib, .a, .macho)
```

### 5.3 Step 1: Define Your AST

```cpp
// include/mylang/AST.hpp
#pragma once
#include <string>
#include <vector>
#include <memory>
#include <cstdint>

namespace mylang {

enum class ExprKind { Literal, Identifier, Binary, Call, Unary };
enum class StmtKind { Return, Let, If, While, Block, Expr };
enum class BinOp    { Add, Sub, Mul, Div, Mod, Eq, Ne, Lt, Le, Gt, Ge };

struct Expr {
    ExprKind kind;
    int64_t  literal_value = 0;        // for Literal
    std::string name;                   // for Identifier / Call
    BinOp binary_op = BinOp::Add;      // for Binary
    std::vector<std::unique_ptr<Expr>> children;  // operands / arguments
};

struct Stmt {
    StmtKind kind;
    std::unique_ptr<Expr> expr;         // return value, condition, etc.
    std::string var_name;               // for Let
    std::vector<std::unique_ptr<Stmt>> body;      // block / if-body
    std::vector<std::unique_ptr<Stmt>> else_body;  // else branch
};

struct Function {
    std::string name;
    std::vector<std::string> params;
    std::vector<std::unique_ptr<Stmt>> body;
};

struct Program {
    std::vector<Function> functions;
};

} // namespace mylang
```

### 5.4 Step 2: Define Your MIR

```cpp
// include/mylang/MIR.hpp
#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace mylang {

struct MirInstruction {
    std::string op;                      // "const_i64", "add_i32", "ret", etc.
    std::vector<std::string> operands;   // Operand values as strings
};

struct MirFunction {
    std::string name;
    std::vector<std::string> parameters;
    std::vector<MirInstruction> instructions;
};

struct MirModule {
    std::vector<MirFunction> functions;
};

} // namespace mylang
```

### 5.5 Step 3: Lower AST → MIR

```cpp
// src/MIR.cpp
#include "mylang/MIR.hpp"
#include "mylang/AST.hpp"
#include <unordered_map>
#include <stdexcept>

namespace mylang {

class MirLowerer {
    uint32_t next_id_ = 0;
    std::unordered_map<std::string, uint32_t> name_to_slot_;
    std::vector<MirInstruction>* out_ = nullptr;

    uint32_t fresh_id() { return ++next_id_; }

    std::string id_str(uint32_t id) { return "%" + std::to_string(id); }

    uint32_t lower_expr(const Expr& e) {
        switch (e.kind) {
        case ExprKind::Literal: {
            uint32_t id = fresh_id();
            out_->push_back({"const_i64", {id_str(id), std::to_string(e.literal_value)}});
            return id;
        }
        case ExprKind::Identifier: {
            auto it = name_to_slot_.find(e.name);
            if (it == name_to_slot_.end())
                throw std::runtime_error("Undefined variable: " + e.name);
            return it->second;
        }
        case ExprKind::Binary: {
            uint32_t lhs = lower_expr(*e.children[0]);
            uint32_t rhs = lower_expr(*e.children[1]);
            uint32_t id  = fresh_id();

            static const char* op_table[] = {
                "add_i32", "sub_i32", "mul_i32", "div_i32", "mod_i32",
                "cmp_eq",  "cmp_ne",  "cmp_lt",  "cmp_le",  "cmp_gt", "cmp_ge"
            };
            const char* op = op_table[static_cast<int>(e.binary_op)];
            out_->push_back({op, {id_str(id), id_str(lhs), id_str(rhs)}});
            return id;
        }
        case ExprKind::Call: {
            std::vector<std::string> ops;
            uint32_t id = fresh_id();
            ops.push_back(id_str(id));
            ops.push_back(e.name);
            for (auto& arg : e.children) {
                ops.push_back(id_str(lower_expr(*arg)));
            }
            out_->push_back({"call", std::move(ops)});
            return id;
        }
        case ExprKind::Unary: {
            uint32_t operand = lower_expr(*e.children[0]);
            uint32_t id = fresh_id();
            out_->push_back({"neg_i32", {id_str(id), id_str(operand)}});
            return id;
        }
        }
        return 0;
    }

    void lower_stmt(const Stmt& s) {
        switch (s.kind) {
        case StmtKind::Return: {
            uint32_t val = lower_expr(*s.expr);
            out_->push_back({"ret", {id_str(val)}});
            break;
        }
        case StmtKind::Let: {
            uint32_t val = lower_expr(*s.expr);
            name_to_slot_[s.var_name] = val;
            break;
        }
        case StmtKind::If: {
            uint32_t cond = lower_expr(*s.expr);
            uint32_t else_label = fresh_id();
            uint32_t end_label  = fresh_id();

            out_->push_back({"br_if_false", {id_str(cond), id_str(else_label)}});
            for (auto& stmt : s.body) lower_stmt(*stmt);
            out_->push_back({"jmp", {id_str(end_label)}});
            out_->push_back({"label", {id_str(else_label)}});
            for (auto& stmt : s.else_body) lower_stmt(*stmt);
            out_->push_back({"label", {id_str(end_label)}});
            break;
        }
        case StmtKind::While: {
            uint32_t loop_label = fresh_id();
            uint32_t end_label  = fresh_id();

            out_->push_back({"label", {id_str(loop_label)}});
            uint32_t cond = lower_expr(*s.expr);
            out_->push_back({"br_if_false", {id_str(cond), id_str(end_label)}});
            for (auto& stmt : s.body) lower_stmt(*stmt);
            out_->push_back({"jmp", {id_str(loop_label)}});
            out_->push_back({"label", {id_str(end_label)}});
            break;
        }
        case StmtKind::Block:
            for (auto& stmt : s.body) lower_stmt(*stmt);
            break;
        case StmtKind::Expr:
            lower_expr(*s.expr);
            break;
        }
    }

public:
    MirModule lower(const Program& program) {
        MirModule module;
        for (const auto& fn : program.functions) {
            MirFunction mir_fn;
            mir_fn.name = fn.name;
            mir_fn.parameters = fn.params;
            out_ = &mir_fn.instructions;
            name_to_slot_.clear();

            // Assign parameter slots
            for (const auto& param : fn.params) {
                uint32_t id = fresh_id();
                name_to_slot_[param] = id;
            }

            // Lower function body
            for (const auto& stmt : fn.body) {
                lower_stmt(*stmt);
            }

            module.functions.push_back(std::move(mir_fn));
        }
        return module;
    }
};

} // namespace mylang
```

### 5.6 Step 4: The DGTL Bridge — The Critical Layer

This is where your language meets DGTL. There are **two approaches**:

---

#### Approach A: In-Process Static Linking (Recommended for Performance)

```cpp
// include/mylang/DGTLBridge.hpp
#pragma once

// Guard: only compile DGTL integration if headers are available
#ifdef MYLANG_HAS_DGTL

#include "dgtl.h"
#include "dgtl_core.h"
#include "dgtl_simd.h"
#include "mylang/MIR.hpp"

namespace mylang {

struct EmitResult {
    bool ok;
    std::string message;
    std::string output_path;
};

class DGTLBridge {
public:
    /// Emit a MIR module to a specific backend target
    EmitResult emit(const MirModule& module, dgtl_target_t target,
                    const std::string& output_path);

    /// Emit to all 9 backends at once
    std::vector<EmitResult> emit_all(const MirModule& module,
                                      const std::string& output_dir);
};

} // namespace mylang

#endif // MYLANG_HAS_DGTL
```

```cpp
// src/DGTLBridge.cpp
#ifdef MYLANG_HAS_DGTL

#include "mylang/DGTLBridge.hpp"
#include <unordered_map>
#include <iostream>
#include <cstdlib>  // for strtoull

namespace mylang {

// ── Internal: Adapt a single MIR function to a DGTL stream ──

static bool adapt_function_to_stream(
    dgtl_stream_t* stream,
    const MirFunction& fn,
    std::string& error_out)
{
    // Slot mapping: MIR string IDs → DGTL uint32_t slots
    std::unordered_map<std::string, uint32_t> slot_map;

    // Assign parameter slots (parameters are implicitly defined)
    for (size_t i = 0; i < fn.parameters.size(); ++i) {
        // Parameters get slots starting from an initial constant
        uint32_t param_slot = dgtl_emit_const_i64(stream, 0);
        slot_map["%" + std::to_string(i + 1)] = param_slot;
    }

    auto resolve = [&](const std::string& operand) -> uint32_t {
        auto it = slot_map.find(operand);
        if (it != slot_map.end()) return it->second;
        error_out = "Unresolved operand: " + operand;
        return 0;
    };

    for (const auto& inst : fn.instructions) {
        const auto& op = inst.op;
        const auto& ops = inst.operands;

        if (op == "const_i64" && ops.size() >= 2) {
            uint64_t imm = std::strtoull(ops[1].c_str(), nullptr, 10);
            uint32_t slot = dgtl_emit_const_i64(stream, imm);
            slot_map[ops[0]] = slot;
        }
        else if (op == "add_i32" && ops.size() >= 3) {
            uint32_t slot = dgtl_emit_add_i32(stream, resolve(ops[1]), resolve(ops[2]));
            slot_map[ops[0]] = slot;
        }
        else if (op == "sub_i32" && ops.size() >= 3) {
            uint32_t slot = dgtl_emit_sub_i32(stream, resolve(ops[1]), resolve(ops[2]));
            slot_map[ops[0]] = slot;
        }
        else if (op == "mul_i32" && ops.size() >= 3) {
            uint32_t slot = dgtl_emit_mul_i32(stream, resolve(ops[1]), resolve(ops[2]));
            slot_map[ops[0]] = slot;
        }
        else if (op == "div_i32" && ops.size() >= 3) {
            uint32_t slot = dgtl_emit_div_i32(stream, resolve(ops[1]), resolve(ops[2]));
            slot_map[ops[0]] = slot;
        }
        else if (op == "mod_i32" && ops.size() >= 3) {
            uint32_t slot = dgtl_emit_mod_i32(stream, resolve(ops[1]), resolve(ops[2]));
            slot_map[ops[0]] = slot;
        }
        else if ((op == "cmp_eq" || op == "cmp_ne" || op == "cmp_lt" ||
                  op == "cmp_le" || op == "cmp_gt" || op == "cmp_ge") && ops.size() >= 3)
        {
            // Map comparison string to DGTL opcode
            static const std::unordered_map<std::string, dgtl_opcode_t> cmp_map = {
                {"cmp_eq", DGTL_OP_CMP_EQ}, {"cmp_ne", DGTL_OP_CMP_NE},
                {"cmp_lt", DGTL_OP_CMP_LT}, {"cmp_le", DGTL_OP_CMP_LE},
                {"cmp_gt", DGTL_OP_CMP_GT}, {"cmp_ge", DGTL_OP_CMP_GE},
            };
            uint32_t slot = dgtl_emit_op(stream, cmp_map.at(op),
                                          resolve(ops[1]), resolve(ops[2]),
                                          0, DGTL_FLAG_NONE);
            slot_map[ops[0]] = slot;
        }
        else if (op == "label" && ops.size() >= 1) {
            uint64_t label_id = std::strtoull(ops[0].substr(1).c_str(), nullptr, 10);
            dgtl_emit_op(stream, DGTL_OP_LABEL, 0, 0, label_id, DGTL_FLAG_NONE);
        }
        else if (op == "jmp" && ops.size() >= 1) {
            uint64_t target = std::strtoull(ops[0].substr(1).c_str(), nullptr, 10);
            dgtl_emit_op(stream, DGTL_OP_JMP, 0, 0, target, DGTL_FLAG_NONE);
        }
        else if ((op == "br_if" || op == "br_if_false") && ops.size() >= 2) {
            uint64_t target = std::strtoull(ops[1].substr(1).c_str(), nullptr, 10);
            dgtl_emit_op(stream, DGTL_OP_BR_IF, resolve(ops[0]), 0,
                          target, DGTL_FLAG_NONE);
        }
        else if (op == "call" && ops.size() >= 2) {
            // call %result callee_name [args...]
            // For now, emit as a CALL opcode with callee encoded in immediate
            uint32_t slot = dgtl_emit_op(stream, DGTL_OP_CALL, 0, 0,
                                          0, DGTL_FLAG_HAS_SIDE_EFFECT);
            slot_map[ops[0]] = slot;
        }
        else if (op == "ret" && ops.size() >= 1) {
            dgtl_emit_return(stream, resolve(ops[0]));
        }
        else if (op == "ret_void") {
            uint32_t zero = dgtl_emit_const_i64(stream, 0);
            dgtl_emit_return(stream, zero);
        }
        // Silently skip: drop, drop_heap (DGTL handles via DCE)
    }

    return error_out.empty();
}

// ── Public API ──

EmitResult DGTLBridge::emit(const MirModule& module, dgtl_target_t target,
                             const std::string& output_path)
{
    EmitResult result;
    dgtl_context_t* ctx = dgtl_context_create();

    for (const auto& fn : module.functions) {
        dgtl_stream_t* stream = dgtl_stream_create(ctx, 4096);

        std::string error;
        if (!adapt_function_to_stream(stream, fn, error)) {
            dgtl_stream_destroy(stream);
            dgtl_context_destroy(ctx);
            result.ok = false;
            result.message = "Adaptation failed for " + fn.name + ": " + error;
            return result;
        }

        // Optimize with default profile
        dgtl_optimize(stream);

        // Emit to file
        dgtl_status_t status = dgtl_emit_to_file(stream, target, output_path.c_str());
        dgtl_stream_destroy(stream);

        if (!status.ok) {
            dgtl_context_destroy(ctx);
            result.ok = false;
            result.message = status.message;
            return result;
        }
    }

    dgtl_context_destroy(ctx);
    result.ok = true;
    result.output_path = output_path;
    return result;
}

std::vector<EmitResult> DGTLBridge::emit_all(const MirModule& module,
                                               const std::string& output_dir)
{
    std::vector<EmitResult> results;
    static const dgtl_target_t targets[] = {
        DGTL_TARGET_ARM64, DGTL_TARGET_X64,
        DGTL_TARGET_NASM,  DGTL_TARGET_WASM
    };

    for (auto target : targets) {
        std::string ext = dgtl_target_extension(target);
        std::string path = output_dir + "/output" + ext;
        results.push_back(emit(module, target, path));
    }
    return results;
}

} // namespace mylang

#endif // MYLANG_HAS_DGTL
```

---

#### Approach B: Out-of-Process via Payload File (Loose Coupling)

```cpp
// src/DGTLBridge_Payload.cpp — Out-of-process approach
#ifdef MYLANG_HAS_DGTL

#include "dgtl.h"
#include "mylang/MIR.hpp"
#include <cstdlib>   // system()
#include <string>

namespace mylang {

/// Write MIR as a .dgtl.payload file, then invoke dgtl_release.exe
bool emit_via_payload(const MirModule& module,
                       const std::string& dgtl_exe_path,
                       const std::string& payload_path,
                       const std::string& output_dir,
                       const std::string& target)   // "arm64", "x64", "nasm", "wasm"
{
    // 1. Build a DGTL stream and serialize to payload
    dgtl_context_t* ctx = dgtl_context_create();
    dgtl_stream_t*  s   = dgtl_stream_create(ctx, 4096);

    // ... (adapt MIR to stream using same logic as Approach A) ...

    dgtl_status_t ws = dgtl_write_payload(s, payload_path.c_str());
    dgtl_stream_destroy(s);
    dgtl_context_destroy(ctx);

    if (!ws.ok) return false;

    // 2. Invoke DGTL CLI to process the payload
    std::string cmd = dgtl_exe_path
        + " --frontend-payload " + payload_path
        + " --backend " + target
        + " --output-dir " + output_dir
        + " --no-print";

    int exit_code = std::system(cmd.c_str());
    return exit_code == 0;
}

} // namespace mylang

#endif
```

### 5.7 Step 5: Build Script

```powershell
# build.ps1 — Build your language with DGTL SDK

$CXX          = "g++"
$STD          = "-std=c++26"
$OPT          = "-O2"
$WARNINGS     = "-Wall -Wextra -Wpedantic"
$DGTL_SDK     = "./deps/DGTL_SDK"
$DGTL_INCLUDE = "-I $DGTL_SDK/include"
$DGTL_LIB     = "$DGTL_SDK/lib/libdgtl.a"   # or dgtl.lib on MSVC
$MYLANG_INC   = "-I ./include"
$DEFINES      = "-DMYLANG_HAS_DGTL=1"

$SOURCES = @(
    "src/main.cpp",
    "src/Lexer.cpp",
    "src/Parser.cpp",
    "src/Semantic.cpp",
    "src/MIR.cpp",
    "src/DGTLBridge.cpp"
)

$OUT = "build/bin/mylangc.exe"

Write-Host "Building MyLang compiler..."
& $CXX $STD $OPT $WARNINGS $MYLANG_INC $DGTL_INCLUDE $DEFINES `
    $SOURCES $DGTL_LIB -o $OUT

if ($LASTEXITCODE -eq 0) {
    Write-Host "SUCCESS: $OUT"
} else {
    Write-Host "FAILED" -ForegroundColor Red
    exit 1
}
```

### 5.8 Step 6: Your Compiler main.cpp

```cpp
// src/main.cpp — Minimal language compiler driver
#include "mylang/AST.hpp"
#include "mylang/MIR.hpp"

#ifdef MYLANG_HAS_DGTL
#include "mylang/DGTLBridge.hpp"
#endif

#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: mylangc <source.mylang> [--target arm64|x64|nasm|wasm] [--emit-all]\n";
        return 1;
    }

    std::string source_path = argv[1];
    std::string target_str  = "x64";
    bool emit_all = false;

    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--target" && i + 1 < argc) target_str = argv[++i];
        if (arg == "--emit-all") emit_all = true;
    }

    // ── Phase 1: Lex ──
    // TokenBuffer tokens = Lexer::tokenize(source_path);

    // ── Phase 2: Parse ──
    // mylang::Program program = Parser::parse(tokens);

    // ── Phase 3: Semantic Analysis ──
    // SemanticAnalyzer::check(program);

    // ── Phase 4: Lower to MIR ──
    // mylang::MirLowerer lowerer;
    // mylang::MirModule mir = lowerer.lower(program);

    // ── Placeholder: Build a trivial MIR for demonstration ──
    mylang::MirModule mir;
    mylang::MirFunction fn;
    fn.name = "main";
    fn.instructions = {
        {"const_i64", {"%1", "42"}},
        {"const_i64", {"%2", "58"}},
        {"add_i32",   {"%3", "%1", "%2"}},
        {"ret",       {"%3"}}
    };
    mir.functions.push_back(std::move(fn));

    // ── Phase 5: Emit via DGTL ──
#ifdef MYLANG_HAS_DGTL
    mylang::DGTLBridge bridge;

    if (emit_all) {
        auto results = bridge.emit_all(mir, "build/out");
        for (const auto& r : results) {
            if (r.ok) std::cout << "OK: " << r.output_path << "\n";
            else      std::cerr << "FAIL: " << r.message << "\n";
        }
    } else {
        // Map string to target enum
        dgtl_target_t target = DGTL_TARGET_X64;
        if (target_str == "arm64") target = DGTL_TARGET_ARM64;
        else if (target_str == "nasm") target = DGTL_TARGET_NASM;
        else if (target_str == "wasm") target = DGTL_TARGET_WASM;
        else if (target_str == "elfobj") target = DGTL_TARGET_ELF_OBJ;
        else if (target_str == "coffobj") target = DGTL_TARGET_COFF_OBJ;
        else if (target_str == "cofflib") target = DGTL_TARGET_COFF_LIB;
        else if (target_str == "staticlib") target = DGTL_TARGET_STATIC_LIB;
        else if (target_str == "macho") target = DGTL_TARGET_MACHO;

        auto result = bridge.emit(mir, target, "build/out/output" + std::string(dgtl_target_extension(target)));
        if (result.ok) std::cout << "Emitted: " << result.output_path << "\n";
        else           std::cerr << "Error: " << result.message << "\n";
    }
#else
    std::cerr << "Warning: DGTL not available; skipping code generation.\n";
#endif

    return 0;
}
```

---

## 6. The MYZON Pattern — Production Reference

MYZON is the **reference language** built on DGTL. Here's how it works at production scale.

### 6.1 Pipeline Overview

```
MYZON Source (.my)
    ↓ Lexer (200+ token types)
TokenBuffer
    ↓ Recursive-Descent + Pratt Expression Parser
AST (Program with 88 grammar rules)
    ↓ 9 Semantic Passes:
    │  1. Symbol Resolution
    │  2. AST Validation
    │  3. Name Binding
    │  4. Type Inference
    │  5. Pattern Exhaustiveness
    │  6. Mutability Checking
    │  7. Definite Assignment
    │  8. Dead Code Detection
    │  9. Ownership Enforcement
Typed AST
    ↓ Lowering::lower_to_mir()
MirModule (string-based SSA-like IR)
    ├─→ Lowering::adapt_mir_to_frontend_lowering_function()
    │       ↓ verify_dgtl_frontend_link_contract()   ← ABI check!
    │       ↓
    │   FrontendLoweringFunction (DGTL's intake format)
    │       ↓ FrontendPipelineLowerer::lower_function()
    │       ↓ GVN + DCE optimizations
    │       ↓ Target-specific instruction selection
    │       ↓
    │   Machine Code (9 targets)
    │
    └─→ Lowering::write_dgtl_frontend_payload()
            ↓ Serialize to .dgtl.payload
            ↓ Invoke dgtl_release.exe externally
            ↓
        Machine Code
```

### 6.2 Conditional Compilation Guard

MYZON compiles **with or without** DGTL headers. The pattern:

```cpp
#ifdef MYZON_HAS_DGTL
#include "dgtl/api/Backend_Driver_API.hpp"
#include "dgtl/api/Function.hpp"
#include "dgtl/api/Frontend_Pipeline.hpp"
#include "dgtl/api/Frontend_Payload.hpp"
#include "dgtl/ir/base/IR_Core.hpp"
// ... all DGTL integration code ...
#endif
```

- **With DGTL:** Full backend emission, payload writing, all 9 targets
- **Without DGTL:** Semantic analysis, AST dump, MIR generation still work; emit gracefully skipped with warning

### 6.3 Value ID Contract

This is **critical** for correctness:

```
Parameters:   IDs 1 .. N (from function signature)
Instructions: IDs N+1 .. max (one unique ID per instruction)

Rules:
  • ID 0 is reserved (invalid/void)
  • Every instruction result gets a unique, monotonically increasing ID
  • Every operand reference must point to a previously defined ID
  • IDs never reused within a function
```

**Example:**
```
Parameter "a" → value_id: 1
Parameter "b" → value_id: 2
add_i32        → result_value_id: 3, lhs: 1, rhs: 2
ret            → ret_value_id: 3
```

### 6.4 Type Metadata

MYZON tags every value with a constant type handle:

```cpp
constexpr dgtl::core::FrontendTypeHandle kMyzonI32Type = 0x1001;

// For every lowered instruction:
lowered.type_handle      = kMyzonI32Type;
lowered.return_abi_class = DGTLABIValueClass::i32;
```

Your language should define its own type handle constants and map them to DGTL ABI classes.

### 6.5 MIR Instruction → DGTL Mapping

| Your MIR | DGTL Mapping | Notes |
|----------|-------------|-------|
| `const_i64 %r imm` | `dgtl_emit_const_i64(s, imm)` | Returns slot for `%r` |
| `add_i32 %r %a %b` | `dgtl_emit_add_i32(s, slot[a], slot[b])` | Arithmetic |
| `sub_i32 %r %a %b` | `dgtl_emit_sub_i32(s, slot[a], slot[b])` | |
| `mul_i32 %r %a %b` | `dgtl_emit_mul_i32(s, slot[a], slot[b])` | |
| `div_i32 %r %a %b` | `dgtl_emit_div_i32(s, slot[a], slot[b])` | |
| `mod_i32 %r %a %b` | `dgtl_emit_mod_i32(s, slot[a], slot[b])` | |
| `cmp_eq %r %a %b` | `dgtl_emit_op(s, DGTL_OP_CMP_EQ, ...)` | Returns 0/1 |
| `cmp_ne/lt/le/gt/ge` | Same pattern with corresponding opcode | |
| `label %id` | `dgtl_emit_op(s, DGTL_OP_LABEL, 0, 0, id, 0)` | Block marker |
| `jmp %target` | `dgtl_emit_op(s, DGTL_OP_JMP, 0, 0, target, 0)` | Unconditional |
| `br_if %c %t` | `dgtl_emit_op(s, DGTL_OP_BR_IF, slot[c], 0, t, 0)` | Conditional |
| `call %r callee args` | `dgtl_emit_op(s, DGTL_OP_CALL, ...)` | Function call |
| `ret %v` | `dgtl_emit_return(s, slot[v])` | Return value |
| `ret_void` | `dgtl_emit_const_i64(s,0)` + `dgtl_emit_return(s, slot)` | Void → 0 |
| `drop %v` | *(skip — DGTL handles via DCE)* | |
| `alloc_huge %r %sz` | `dgtl_emit_op(s, DGTL_OP_CALL, ...)` → intrinsic | Heap alloc |
| `drop_heap %ptr` | `dgtl_emit_op(s, DGTL_OP_CALL, ...)` → intrinsic | Heap free |

---

## 7. Optimization Pipeline

### Default Pipeline (`dgtl_optimize`)

1. **GVN (Global Value Numbering)** — CSE + constant folding + PHI folding
2. **DCE (Dead Code Elimination)** — Dense + sparse liveness tracking
3. **Constant Folding** — Commutative optimization, strength reduction

### Profile-Specific Passes

| Profile | What It Does | When to Use |
|---------|-------------|-------------|
| `DGTL_OPT_PROFILE_DEFAULT` | GVN + DCE + folding | General-purpose |
| `DGTL_OPT_PROFILE_FOLD` | Aggressive constant propagation | Compile-time-heavy languages |
| `DGTL_OPT_PROFILE_FUSION` | MUL+ADD→FMA, temporal fusion | Numeric / BLAS-like kernels |
| `DGTL_OPT_PROFILE_SIMD` | Vectorization + SIMD legalization | Data-parallel workloads |
| `DGTL_OPT_PROFILE_PRESSURE` | Register pressure minimization | Tight register targets (ARM) |
| `DGTL_OPT_PROFILE_PREDICATION` | Branch→predication conversion | Branchless HFT paths |

### Macro-Op Fusion

DGTL automatically fuses instruction patterns:

```
MUL + ADD  →  FMA (Fused Multiply-Add)
LOAD + ADD →  Memory-op fusion (where legal)
CMP + BR   →  Predicated branch
```

### Zero-Spill Register Allocator

- Verified up to **25 live variables** without spilling
- Beyond 25: controlled spill with stack slot reuse
- Use `DGTL_OPT_PROFILE_PRESSURE` for tight-register scenarios

### Polyhedral SCoP Tiling

Format your loop nests as SCoP-compatible (Static Control Parts) to trigger:

- **2D Matmul tiling** — Cache-aware blocking
- **Loop fusion** — Reduce memory traffic
- **Auto-parallelization** — Independent iteration detection

```c
// Structure your loops for polyhedral optimization:
// - Affine bounds (linear expressions of outer loop variables)
// - No irregular control flow inside the loop body
// - Memory accesses as affine functions of loop indices

for (int i = 0; i < N; ++i)
    for (int j = 0; j < M; ++j)
        C[i*M+j] = A[i*K+0] * B[0*M+j] + ...;  // ← Triggers SCoP tiling
```

---

## 8. Backend Emission & Multi-Targeting

### Target Support Table

| Target | Enum | Extension | Format | Key Features |
|--------|------|-----------|--------|--------------|
| **ARM64** | `DGTL_TARGET_ARM64` | `.arm64.s` | Text assembly | SIMD widening/narrowing (`fcvtl`/`fcvtn`), NEON |
| **x86-64** | `DGTL_TARGET_X64` | `.x64.bin` | Binary | AVX-512 FMA, compact binary stream |
| **NASM** | `DGTL_TARGET_NASM` | `.nasm.asm` | Text assembly | x86-family, human-readable |
| **WASM** | `DGTL_TARGET_WASM` | `.wasm` | Binary | SIMD128, web-deployable |
| **ELF Object** | `DGTL_TARGET_ELF_OBJ` | `.o` | Binary | `7F 45 4C 46` magic, ET_REL, 6 sections |
| **Static Lib** | `DGTL_TARGET_STATIC_LIB` | `.a` | Archive | GNU ar format |
| **COFF Object** | `DGTL_TARGET_COFF_OBJ` | `.obj` | Binary | `64 86` IMAGE_FILE_MACHINE_AMD64 |
| **COFF Library** | `DGTL_TARGET_COFF_LIB` | `.lib` | Archive | `!<arch>` + linker member |
| **Mach-O** | `DGTL_TARGET_MACHO` | `.macho` | Binary | `CF FA ED FE` MH_MAGIC_64 |

### Emitting Multiple Targets

```c
const dgtl_target_t targets[] = {
    DGTL_TARGET_ARM64, DGTL_TARGET_X64,
    DGTL_TARGET_NASM,  DGTL_TARGET_WASM,
    DGTL_TARGET_ELF_OBJ, DGTL_TARGET_COFF_OBJ
};

for (int i = 0; i < 6; ++i) {
    char path[256];
    snprintf(path, sizeof(path), "build/out/module%s",
             dgtl_target_extension(targets[i]));
    dgtl_status_t s = dgtl_emit_to_file(stream, targets[i], path);
    printf("%s: %s\n", dgtl_target_name(targets[i]), s.ok ? "OK" : s.message);
}
```

### LogicHash Parity

All backends must produce the same LogicHash for the same input IR:

```
ARM64:  logic_hash = 0x9c13c402303ba602  ✓
x64:    logic_hash = 0x9c13c402303ba602  ✓
NASM:   logic_hash = 0x9c13c402303ba602  ✓
WASM:   logic_hash = 0x9c13c402303ba602  ✓
```

---

## 9. SIMD & HFT Stride Alignment

### Enabling HFT Mode

For maximum throughput (250M+ nodes/sec):

```c
dgtl_stream_t* s = dgtl_stream_create(ctx, 65536);  // Large reservation

// Set FMA mode for floating-point pipelines
dgtl_set_ir_mode(s, DGTL_IR_MODE_FMA);

// Legalize for target SIMD architecture
dgtl_legalize_simd(s, DGTL_SIMD_X86_AVX512);

// Strike with optimization
dgtl_optimize_with_profile(s, DGTL_OPT_PROFILE_SIMD);
dgtl_strike_stream(s, DGTL_TARGET_X64);
```

### SIMD Lane Widths

| Architecture | Register Width | Lanes (32-bit) | Lanes (64-bit) |
|-------------|---------------|-----------------|-----------------|
| AVX-512 | 512 bits | 16 | 8 |
| ARM NEON | 128 bits | 4 | 2 |
| WASM SIMD128 | 128 bits | 4 | 2 |

### Vectorized Kernel Example

```c
// Build a dot-product kernel using DGTL SIMD
dgtl_context_t* ctx = dgtl_context_create();
dgtl_stream_t*  s   = dgtl_stream_create(ctx, 1024);

dgtl_set_ir_mode(s, DGTL_IR_MODE_FMA);

// Load vector elements
uint32_t a0 = dgtl_emit_const_i64(s, 10);
uint32_t a1 = dgtl_emit_const_i64(s, 20);
uint32_t b0 = dgtl_emit_const_i64(s, 3);
uint32_t b1 = dgtl_emit_const_i64(s, 4);

// Accumulate via FMA: acc = a0*b0 + 0, then acc = a1*b1 + acc
uint32_t zero = dgtl_emit_const_i64(s, 0);
uint32_t acc  = dgtl_emit_fma(s, a0, b0, zero);     // 10*3 + 0 = 30
         acc  = dgtl_emit_fma(s, a1, b1, acc);       // 20*4 + 30 = 110

dgtl_emit_return(s, acc);

// Legalize + optimize + strike
dgtl_legalize_simd(s, DGTL_SIMD_X86_AVX512);
dgtl_optimize_with_profile(s, DGTL_OPT_PROFILE_FUSION);
dgtl_strike_result_t res = dgtl_strike_stream(s, DGTL_TARGET_X64);

dgtl_stream_destroy(s);
dgtl_context_destroy(ctx);
```

---

## 10. Advanced Integration: Custom Dialects & 

### 10.1 Custom Node Opcodes

Use `dgtl_emit_op` with custom flag combinations to define your own semantics:

```c
// Define your language's custom operations via the generic emitter
#define MYLANG_FLAG_TENSOR_OP   0x0100   // Custom bit in upper flags
#define MYLANG_FLAG_GRADIENT    0x0200   // Gradient tracking

uint32_t tensor_mul = dgtl_emit_op(stream, DGTL_OP_MUL,
    tensor_a, tensor_b, /*immediate=*/0,
    DGTL_FLAG_IS_FP | MYLANG_FLAG_TENSOR_OP);

uint32_t grad_add = dgtl_emit_op(stream, DGTL_OP_FADD,
    grad_a, grad_b, /*immediate=*/0,
    DGTL_FLAG_IS_FP | MYLANG_FLAG_GRADIENT);
```

**Important:** Custom flags in bits 8-15 (upper byte of uint16_t) are reserved for frontend use. DGTL's optimizer respects but does not interpret them.

### 10.2  Execution

 is available on **x64 only** and requires an environment gate:

```c
// Set environment variable before running:
// $env:DGTL_STRIKE_EXECUTE = "1"

dgtl_strike_result_t res = dgtl_strike_stream(s, DGTL_TARGET_X64);
// If DGTL_STRIKE_EXECUTE=1, the generated code is:
//   1. Allocated via VirtualAlloc (Windows) or mmap (Linux)
//   2. Permissions set to PAGE_EXECUTE_READ
//   3. Code injected into executable memory
//   4. Function pointer called
```

The  flow uses `StrikeOrchestrator::fire_strike()`:

```
fire_strike()
  ├── Stage 1: Optimization (GVN, DCE, fusion)
  ├── Stage 2: Emission (target-specific machine code)
  ├── Stage 3: Injection (VirtualAlloc + memcpy)
  └── Stage 4: Execution (function pointer call)
```

### 10.3 Speculative Compilation

DGTL generates **4 speculative variants** per node:

| Variant | Strategy | Confidence |
|---------|----------|------------|
| Baseline | Unmodified | 55% |
| Constant-specialized | Constant propagation | 72% |
| Vector-biased | SIMD widening | 68% |
| Branch-likely | Branch prediction hints | 61% |

The runtime AI cost model selects the best variant via feedback.

### 10.4 Polyhedral Parallelization CLI Flags

```
--polyhedral-mode <off|safe|aggressive>
--polyhedral-tile <n>           # Tile size (cache line multiple)
--polyhedral-parallel <auto|off|on>
```

---

## 11. All Compiler & SDK Flags

### 11.1 DGTL CLI Flags (dgtl_release.exe)

| Flag | Values | Description |
|------|--------|-------------|
| `--frontend-payload` | `<path>` | Load a `.dgtl.payload` file |
| `--backend` | `arm64`, `x64`, `nasm`, `wasm`, `elfobj`, `staticlib`, `coffobj`, `cofflib`, `macho` | Target backend |
| `--emit-all` | *(none)* | Emit all backends |
| `--output-dir` | `<path>` | Output directory |
| `--no-print` | *(none)* | Suppress stdout |
| `--quiet` | *(none)* | Suppress stderr |
| `--dump` | *(none)* | Dump IR and `TR_HASH` backend stream |
| `---execute` | *(none)* |  compile + execute (x64 only) |
| `--polyhedral-mode` | `off`, `safe`, `aggressive` | Polyhedral parallelization |
| `--polyhedral-tile` | `<integer>` | Tile size for polyhedral tiling |
| `--polyhedral-parallel` | `auto`, `off`, `on` | Parallelization control |
| `--ir-mode` | `int`, `float`, `fma` | IR type mode |
| `--arm64-binary-stream` | *(none)* | ARM64 A/B binary stream mode |

### 11.2 Preprocessor Defines (Compile-Time)

| Define | Purpose |
|--------|---------|
| `MYLANG_HAS_DGTL=1` | Enable DGTL integration in your compiler |
| `DGTL_SHARED_BUILD` | Building DGTL as a shared library (DLL) |
| `DGTL_SHARED_IMPORT` | Consuming DGTL as a shared library |
| *(neither defined)* | Static library mode (default, recommended) |
| `DGTL_STRIKE_EXECUTE=1` | Environment variable: enable  execution gate |

### 11.3 Compiler Flags for Building Against DGTL

| Platform | Recommended Flags |
|----------|-------------------|
| **GCC (UCRT64)** | `-std=c++26 -O2 -Wall -Wextra -Wpedantic -I<SDK>/include -L<SDK>/lib -ldgtl` |
| **GCC (Linux)** | `-std=c++26 -O2 -Wall -Wextra -I<SDK>/include -L<SDK>/lib -ldgtl -lpthread` |
| **Clang** | `-std=c++26 -O2 -Wall -Wextra -I<SDK>/include -L<SDK>/lib -ldgtl` |
| **MSVC** | `/std:c++latest /O2 /W4 /I<SDK>\include <SDK>\lib\dgtl.lib` |
| **C-only** | `-std=c17 -O2 -I<SDK>/include -L<SDK>/lib -ldgtl` |

### 11.4 DGTL Build System Commands (dgtl.ps1)

| Command | Description |
|---------|-------------|
| `build-release` | Production binary (`dgtl_release.exe`) |
| `build-smoke` | Development binary (`dgtl_main.exe`) |
| `build-strict` | Compile with `-Werror` |
| `run-all` | Full conformance test suite |
| `run-backend <target>` | Single backend validation |
| `bench-backend <target> [profile] [vars] [dump]` | Run benchmarks |
| `update-goldens` | Refresh expected output baselines |
| `run-strict-conformance` / `srun` | Strict executable validation |
| `test [full]` | Aggregate functional + robustness tests |
| `header-check` | Verify proprietary headers |
| `package` | Full validation gate + SDK distribution build |
| `clean-all` | Remove all outputs |
| `clean-build-and-run` | Full cycle: clean → build → test |

### 11.5 Benchmark Profiles

| Profile | Focus Area |
|---------|-----------|
| `fold` | Constant folding throughput (default) |
| `fusion` | Instruction fusion (MUL+ADD→FMA) |
| `simd` | SIMD vectorization passes |
| `pressure` | Register pressure measurement |
| `predication` | Branch-to-predication conversion |

---

## 12. Error Codes & Troubleshooting

### DGTL SDK Error Codes

| Code | Name | Cause | Fix |
|------|------|-------|-----|
| `0` | Success | — | — |
| `EINVAL` | Invalid argument | NULL pointer, invalid target, malformed IR | Check all pointers and enum values |
| `ENOMEM` | Out of memory | Arena exhaustion | Increase stream reservation or reduce IR size |
| `E_HASH_MISMATCH` | Logic hash mismatch | Nondeterministic IR construction | Ensure deterministic emission order |
| `E_PAYLOAD_VERSION` | Payload version error | Incompatible `.dgtl.payload` format | Regenerate payload with matching SDK version |
| `E_BACKEND_FAIL` | Backend emission failed | Target-specific codegen error | Check IR validity, try `--dump` flag |
| `E_STRIKE_FAIL` | Strike failed |  injection or execution error | Check `DGTL_STRIKE_EXECUTE` env var |

### Your Language's Diagnostic Codes (MYZON Reference)

| Code | Severity | Meaning |
|------|----------|---------|
| `E4006` | Error | `verify_dgtl_frontend_link_contract()` failed — ABI mismatch |
| `E4014` | Error | MIR→DGTL adaptation failed for a function |
| `E4016` | Error | Adaptation error during payload write |
| `E4017` | Error | `write_frontend_payload_file()` failed |
| `I4007` | Info | DGTL link contract verified successfully |
| `I4015` | Info | MIR adaptation stats (functions, instructions) |
| `W4001` | Warning | DGTL headers not found; backend emission skipped |
| `W4008` | Warning | Unresolved identifier in lowering |
| `W4009` | Warning | Non-numeric literal lowered as 0 |
| `W4022` | Warning | Loop unroll limit reached |

### Debugging Workflow

```bash
# 1. Dump the IR and backend stream
dgtl_release.exe --frontend-payload my_module.dgtl.payload --backend x64 --dump

# 2. Check the TR_HASH in output — should match across backends
# Look for: TR_HASH: 0x...

# 3. Inspect node count and logic hash programmatically
size_t count = dgtl_stream_node_count(stream);
uint64_t hash = dgtl_stream_logic_hash(stream);
printf("Nodes: %zu, Hash: 0x%016llx\n", count, hash);

# 4. Walk the IR after optimization
for (size_t i = 0; i < count; ++i) {
    dgtl_node_info_t info;
    dgtl_stream_get_node(stream, i, &info);
    // Inspect: was your MUL+ADD fused to FMA?
    // Are dead nodes flagged with DGTL_FLAG_DEAD?
}
```

---

## 13. Frontend Conformance Kit

To certify your language works correctly with DGTL, pass these test categories:

### 13.1 Payload Schema Compliance

- [ ] Header version matches SDK version
- [ ] Record ordering is correct (functions in declaration order)
- [ ] Escape/unescape roundtrip preserves all identifiers
- [ ] Malformed payloads are rejected gracefully

### 13.2 Lowering Contract Compliance

- [ ] Every function has a terminating instruction (`ret`)
- [ ] Value IDs are defined before use (monotonically increasing)
- [ ] All instruction kinds in your language map to supported DGTL opcodes
- [ ] Call arity matches callee signature

### 13.3 Backend Emission Coverage

All 9 targets must succeed for your test corpus:

- [ ] `DGTL_TARGET_ARM64` — `.arm64.s`
- [ ] `DGTL_TARGET_X64` — `.x64.bin`
- [ ] `DGTL_TARGET_NASM` — `.nasm.asm`
- [ ] `DGTL_TARGET_WASM` — `.wasm`
- [ ] `DGTL_TARGET_ELF_OBJ` — `.o`
- [ ] `DGTL_TARGET_STATIC_LIB` — `.a`
- [ ] `DGTL_TARGET_COFF_OBJ` — `.obj`
- [ ] `DGTL_TARGET_COFF_LIB` — `.lib`
- [ ] `DGTL_TARGET_MACHO` — `.macho`

### 13.4 Determinism

- [ ] Same payload + same options = **stable LogicHash** across runs
- [ ] Same payload emitted to different backends = **same LogicHash**

### 13.5 Mandatory Test Corpus

Your language MUST successfully compile and emit these patterns:

1. **Constants and returns:** `return 42`
2. **Arithmetic chains:** `return (a + b) * (c - d)`
3. **Function calls:** `return add(1, 2)` (including arity mismatch test)
4. **Nested expressions:** `return f(g(x) + h(y, z))`
5. **Control flow:** `if (x > 0) return x; else return -x;`
6. **Loops:** `while (i < 10) { sum = sum + i; i = i + 1; } return sum;`

---

## 14. Performance Tuning Checklist

Before shipping your language with DGTL:

### Memory & Alignment
- [ ] Is all memory passed to DGTL **64-bit aligned** (`alignas(8)`)?
- [ ] Are IR streams reserved with sufficient capacity to avoid reallocation?
- [ ] Are you reusing `dgtl_context_t` across multiple compilations (not creating/destroying per function)?

### Optimization
- [ ] Have you called `dgtl_optimize()` (or `dgtl_optimize_with_profile()`) before striking?
- [ ] Are you using `DGTL_OPT_PROFILE_FUSION` for numeric-heavy code?
- [ ] Have you enabled `DGTL_OPT_PROFILE_SIMD` for data-parallel loops?
- [ ] Is `DGTL_IR_MODE_FMA` set for floating-point pipelines?

### Backend
- [ ] Are you targeting the correct architecture for your deployment?
- [ ] Have you verified LogicHash parity across all backends?
- [ ] For : is `DGTL_STRIKE_EXECUTE=1` set in the environment?

### IR Quality
- [ ] Are your value IDs monotonically increasing with no gaps?
- [ ] Does every function end with a `ret` instruction?
- [ ] Are unused values avoided? (DGTL's DCE will clean up, but less IR = faster compilation)
- [ ] Are loop nests SCoP-compatible for polyhedral optimization?

### Scale
- [ ] Have you tested with 262K+ nodes? (DGTL's validated parallel threshold)
- [ ] Have you benchmarked with the `pressure` profile for register-constrained code?
- [ ] Is your compile-time const-folding reducing payload size before DGTL sees it?

---

## 15. Licensing & Compliance

### Proprietary License

DGTL SDK is proprietary software of **TataG Systems (2026)**. Key terms:

- **No license is granted** except under a separate written agreement
- **Cannot be used** for production, commercial, or redistribution purposes without a license
- Provided **AS-IS** without warranties
- Contact: `licensing@dgtl.tech`

### Mandatory Copyright Header

All files extending DGTL must include:

```
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
```

Run `dgtl.ps1 header-check` to verify compliance across your project.

### API Tier Stability Guarantees

| Tier | Scope | Backward Compatibility |
|------|-------|----------------------|
| **A (Stable)** | `dgtl.h`, payload schema, emission API | Full semver guarantee. Deprecation requires 1 minor cycle. |
| **B (Power)** | `dgtl_core.h`, `dgtl_simd.h`, optimization profiles | Best-effort. May change in minor versions with migration notes. |
| **C (Internal)** | Everything under `include/dgtl/` (non-SDK) | No guarantee. Implementation details only. |

### Dependency Baseline

| Component | Version |
|-----------|---------|
| C++ Standard | **C++26** |
| C Standard | **C17** |
| Compiler | **GCC 15.2.0** (UCRT64) |
| OS | **Windows** (PowerShell 5.1+) |
| Runtime ABI | **UCRT64** |

---

## Appendix A: Complete Payload Format

```
DGTL_FRONTEND_PAYLOAD_V1
FUNCTION <name>
PARAM <name>
PARAM <name>
INSTRUCTION <kind> <result_value_id> <lhs_value_id> <rhs_value_id> <immediate>
INSTRUCTION <kind> <result_value_id> <ret_value_id>
END_FUNCTION
```

**Example:**
```
DGTL_FRONTEND_PAYLOAD_V1
FUNCTION add
PARAM a
PARAM b
INSTRUCTION add_i32 3 1 2 0
INSTRUCTION ret 4 3
END_FUNCTION
```

## Appendix B: Target Extension Quick Reference

```c
dgtl_target_extension(DGTL_TARGET_ARM64)      → ".arm64.s"
dgtl_target_extension(DGTL_TARGET_X64)        → ".x64.bin"
dgtl_target_extension(DGTL_TARGET_NASM)       → ".nasm.asm"
dgtl_target_extension(DGTL_TARGET_WASM)       → ".wasm"
dgtl_target_extension(DGTL_TARGET_ELF_OBJ)    → ".o"
dgtl_target_extension(DGTL_TARGET_STATIC_LIB) → ".a"
dgtl_target_extension(DGTL_TARGET_COFF_OBJ)   → ".obj"
dgtl_target_extension(DGTL_TARGET_COFF_LIB)   → ".lib"
dgtl_target_extension(DGTL_TARGET_MACHO)       → ".macho"
```

## Appendix C: Environment Variables

| Variable | Effect |
|----------|--------|
| `DGTL_STRIKE_EXECUTE=1` | Enable  execution (required for `---execute`) |
| `DGTL_ARENA_SIZE=<bytes>` | Override per-thread arena size (default: 64 MB) |
| `DGTL_THREAD_SLOTS=<n>` | Override TLS slot count (default: 64, range: 1–64) |

---

*This document is the canonical reference for building programming languages on the DGTL backend. For questions, contact TataG Systems.*
