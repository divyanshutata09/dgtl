# DGTL Public SDK: Developer Integration Guide (v1.2.0)

This document provides a production-grade specification of the Deep General-purpose Transformation Layer (DGTL) public C-API. It is designed to enable developers to build compilers and runtime accelerators in **any programming language** (C, C++, Rust, Python, Go, Odin, Zig, etc.) that supports a Foreign Function Interface (FFI).

---

## 1. SDK Distribution Layout

The packaged DGTL client distribution is structured as follows:

```text
DGTL_SDK/
├── include/
│   └── dgtl_sdk/
│       ├── dgtl.h              <-- Umbrella Header (Include this for C/C++)
│       ├──         <-- Pure C ABI & Stream Introspection
│       ├── dgtl_capabilities.h <-- Capability Matrix Queries
│       ├── dgtl_core.h         <-- Core Types & Emitters
│       ├── dgtl_session.h      <-- JIT/AOT Session Execution
│       └──          <-- Vector/SIMD Emitters
└── lib/
    └── x64/
        └── dgtl.lib            <-- Ingestion Link Library (Windows)
```

---

## 2. Cross-Language ABI Type Mappings

To interface with the opaque pointer architecture of DGTL, map the C ABI types to your language's primitives as defined below:

| C ABI Type | Description | Rust Equivalent | Python (`ctypes`) | Go Equivalent | Odin Equivalent |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `dgtl_context_t*` | Context Handle | `*mut c_void` | `ctypes.c_void_p` | `unsafe.Pointer` | `rawptr` |
| `dgtl_stream_t*` | IR Stream Handle | `*mut c_void` | `ctypes.c_void_p` | `unsafe.Pointer` | `rawptr` |
| `dgtl_session_t*` | Compile Session Handle | `*mut c_void` | `ctypes.c_void_p` | `unsafe.Pointer` | `rawptr` |
| `dgtl_status_t` | Response Struct | `DGTLStatus` (struct) | `class DGTLStatus(Structure)` | `struct DGTLStatus` | `DGTL_Status` (struct) |
| `uint32_t` | Register/Value Slot ID | `u32` | `ctypes.c_uint32` | `uint32` | `u32` |
| `dgtl_opcode_t` | Instruction Opcode | `u32` (or enum) | `ctypes.c_uint32` | `uint32` | `u32` |

---

## 3. Memory & Threading Contracts

### Memory Management
DGTL handles follow a strict **create-destroy ownership model**. The language frontend is responsible for destroying allocated handles to prevent memory leaks:
* `dgtl_context_create` -> Must be freed using `dgtl_context_destroy(context)`.
* `dgtl_stream_create` -> Must be freed using `dgtl_stream_destroy(stream)`.
* `dgtl_session_create` -> Must be freed using `dgtl_session_destroy(session)`.

### Thread Safety Rules
1. **Context Isolation**: A `dgtl_context_t` is thread-safe and can be shared across multiple threads.
2. **Stream Concurrency**: A `dgtl_stream_t` is **not thread-safe**. Only one thread can emit instructions into a specific stream at a time.
3. **Session Compilation**: Multiple threads can compile independent streams concurrently using different `dgtl_session_t` handles.

---

## 4. Robust Error Propagation & Recovery

Always query status structures to handle execution and compilation failures gracefully:

```cpp
#include "dgtl_sdk/dgtl.h"
#include <iostream>

void check_status(dgtl_status_t status) {
    if (!status.ok) {
        // Query the detailed thread-local error message
        const char* err_msg = dgtl_get_last_error();
        std::cerr << "[DGTL Error] Code: " << status.error_code 
                  << " Message: " << err_msg << std::endl;
        
        // Clear error thread-local state
        dgtl_clear_error();
    }
}
```

---

## 5. End-to-End FFI Boilerplate by Language

Below are copy-pasteable, production-ready FFI bindings and scripts for integrating DGTL into your frontend language of choice.

### Rust
```rust
use std::ffi::{c_char, c_void, CStr};

#[repr(C)]
pub struct DGTLStatus {
    pub ok: i32,
    pub error_code: i32,
    pub message: [c_char; 256],
}

#[link(name = "dgtl")]
extern "C" {
    // Calling convention defaults to __cdecl on Windows for C linkage
    pub fn dgtl_context_create() -> *mut c_void;
    pub fn dgtl_context_destroy(ctx: *mut c_void);
    pub fn dgtl_stream_create(ctx: *mut c_void, capacity: usize) -> *mut c_void;
    pub fn dgtl_stream_destroy(stream: *mut c_void);
    pub fn dgtl_emit_const_i64(stream: *mut c_void, value: u64) -> u32;
    pub fn dgtl_get_last_error() -> *const c_char;
}

fn main() {
    unsafe {
        let ctx = dgtl_context_create();
        if ctx.is_null() {
            panic!("Failed to create DGTL context");
        }
        let stream = dgtl_stream_create(ctx, 256);
        let reg = dgtl_emit_const_i64(stream, 42);
        println!("Emitted constant 42 to virtual register slot: {}", reg);
        
        dgtl_stream_destroy(stream);
        dgtl_context_destroy(ctx);
    }
}
```

### Python
```python
import ctypes
from ctypes import Structure, c_int, c_char, c_void_p, c_size_t, c_uint32, c_uint64

class DGTLStatus(Structure):
    _fields_ = [
        ("ok", c_int),
        ("error_code", c_int),
        ("message", c_char * 256)
    ]

# Load public SDK DLL
dgtl = ctypes.CDLL("./dgtl.dll")

# Define parameter and return types
dgtl.dgtl_context_create.restype = c_void_p
dgtl.dgtl_context_destroy.argtypes = [c_void_p]

dgtl.dgtl_stream_create.argtypes = [c_void_p, c_size_t]
dgtl.dgtl_stream_create.restype = c_void_p
dgtl.dgtl_stream_destroy.argtypes = [c_void_p]

dgtl.dgtl_emit_const_i64.argtypes = [c_void_p, c_uint64]
dgtl.dgtl_emit_const_i64.restype = c_uint32

# Execution
ctx = dgtl.dgtl_context_create()
if not ctx:
    raise RuntimeError("Failed to allocate DGTL Context")

stream = dgtl.dgtl_stream_create(ctx, 512)
val_reg = dgtl.dgtl_emit_const_i64(stream, 999)
print(f"Allocated value to virtual slot: {val_reg}")

dgtl.dgtl_stream_destroy(stream)
dgtl.dgtl_context_destroy(ctx)
```

### Go
```go
package main

/*
#cgo LDFLAGS: -ldgtl
#include <stdlib.h>
#include "dgtl_sdk/dgtl.h"
*/
import "C"
import (
	"fmt"
	"unsafe"
)

type Context struct {
	handle unsafe.Pointer
}

func NewContext() (*Context, error) {
	handle := C.dgtl_context_create()
	if handle == nil {
		return nil, fmt.Errorf("failed to create context")
	}
	return &Context{handle: handle}, nil
}

func (c *Context) Close() {
	C.dgtl_context_destroy(c.handle)
}

func main() {
	ctx, err := NewContext()
	if err != nil {
		panic(err)
	}
	defer ctx.Close()
	fmt.Println("DGTL context initialized successfully")
}
```

### Odin
``` Odin
package dgtl_binding

import "core:fmt"
import "core:c"

foreign import dgtl "system:dgtl.lib"

DGTL_Status :: struct {
    ok:         c.int,
    error_code: c.int,
    message:    [256]c.char,
}

@(default_calling_convention="c")
foreign dgtl {
    dgtl_context_create  :: proc() -> rawptr ---
    dgtl_context_destroy :: proc(ctx: rawptr) ---
    dgtl_stream_create   :: proc(ctx: rawptr, capacity: c.size_t) -> rawptr ---
    dgtl_stream_destroy  :: proc(stream: rawptr) ---
    dgtl_emit_const_i64  :: proc(stream: rawptr, value: u64) -> u32 ---
}

main :: proc() {
    ctx := dgtl_context_create()
    if ctx == nil {
        fmt.eprintln("Failed to initialize DGTL context")
        return
    }
    defer dgtl_context_destroy(ctx)

    stream := dgtl_stream_create(ctx, 256)
    defer dgtl_stream_destroy(stream)

    reg_id := dgtl_emit_const_i64(stream, 100)
    fmt.printf("Successfully emitted constant at register: %d\n", reg_id)
}
```

---

## 6. Advanced Compiler Lowering Features

Use these emitters to control execution properties directly from your compiler frontend:

### Predictive Branching
For HFT (High-Frequency Trading) loops where microsecond branch mispredictions are costly:
```cpp
// Hints to the CPU compiler engine that condition is highly likely to jump to true_label
dgtl_emit_branch_predictive(stream, condition_slot, true_label_id, false_label_id, 98); // 98% likelihood
```

### Hardware Affinity Boundaries
Injects thread fences or memory placement barriers:
```cpp
// Pins execution layout to the NUMA node mask loaded into mask_register_id
dgtl_emit_hardware_affinity(stream, mask_register_id);
```

### 512-bit Vector SIMD Operations
Natively compile parallel vector mathematical flows:
```cpp
// Emit a hardware vector float Fused Multiply-Add instruction
uint32_t result_vec = dgtl_emit_fma(stream, vec_a, vec_b, vec_c);
```

---

## 7. Compilation & Linking Flags

### Windows (MSVC)
* **Include Path**: `/I"path/to/DGTL_SDK/include"`
* **Library Path**: `/LIBPATH:"path/to/DGTL_SDK/lib/x64"`
* **Link Library**: `dgtl.lib`

### GCC / Clang (Linux/Unix)
* **Include Path**: `-Ipath/to/DGTL_SDK/include`
* **Library Path**: `-Lpath/to/DGTL_SDK/lib/x64`
* **Link Library**: `-ldgtl`

---

## 8. Lowering Arbitrary Integer & Float Widths

Since standard CPU registers operate on 32-bit and 64-bit limits, frontends lower alternate type widths using promotion, masking, and multi-register splitting:

### A. Sub-word Integers (`i8`, `i16`, `u8`, `u16`)
Frontends store sub-word integers in native `i32` slots and emulate truncation during operations:

1. **Promotion**: Lower any sub-word mathematical variable using `DGTL_OP_ADD`, `DGTL_OP_SUB`, etc. (the native `i32` instructions).
2. **Masking (Truncation)**: When storing a value, returning a value, or comparing it, insert a bitwise `AND` operation to discard overflow bits:
   ```cpp
   // To lower: let result: i8 = a + b;
   uint32_t temp_sum = dgtl_emit_op_with_result(stream, 0, DGTL_OP_ADD, val_a, val_b, 0, DGTL_FLAG_NONE);
   
   // Emit truncation: temp_sum & 0xFF
   uint32_t mask_val = dgtl_emit_const_i64(stream, 0xFF);
   uint32_t val_res  = dgtl_emit_op_with_result(stream, 0, DGTL_OP_AND, temp_sum, mask_val, 0, DGTL_FLAG_NONE);
   ```

### B. Sub-word Floats (`f16`, `bf16`)
1. **Conversion**: Promote variables to standard floating-point `f32` representations before operations.
2. **Narrowing**: Execute math using `DGTL_OP_FADD` / `DGTL_OP_FMUL`, and truncate using vector conversion rules (e.g. `FCVTN_F32_TO_F16`) when packing into arrays.

### C. Large Primitives (`i128`, `f128`)
For widths exceeding the 64-bit hardware limit:
1. **Split Allocation**: Map the variable to two separate `i64` slots (low part and high part).
2. **Carry Propagation**: Emit math by linking the slots with carry checkers (e.g., adding the low parts, checking overflow, and adding high parts with carry propagation).
3. **FFI Runtime Helpers**: Register host library calls (such as compiler-rt's `__multi3` or `__addtf3`) and emit calling instructions (`DGTL_OP_CALL`) to pass execution to the runtime library.

---

## 9. C++ Universal Wrapper Pattern (Reference Design)

For C++ compiler frontends, implement an RAII class layout to encapsulate opaque handles, error propagation, CFG introspection, and stream serialization:

```cpp
#include "dgtl_sdk/dgtl.h"
#include <string>
#include <vector>
#include <memory>
#include <stdexcept>

// RAII Wrapper for context and stream resources
class DGTLStreamWrapper {
private:
    dgtl_context_t* context_ = nullptr;
    dgtl_stream_t*  stream_ = nullptr;

public:
    DGTLStreamWrapper(size_t initial_capacity = 256) {
        context_ = dgtl_context_create();
        if (!context_) throw std::runtime_error("Failed to allocate DGTL Context");
        
        stream_ = dgtl_stream_create(context_, initial_capacity);
        if (!stream_) {
            dgtl_context_destroy(context_);
            throw std::runtime_error("Failed to allocate DGTL Stream");
        }
    }

    ~DGTLStreamWrapper() {
        if (stream_) dgtl_stream_destroy(stream_);
        if (context_) dgtl_context_destroy(context_);
    }

    dgtl_stream_t* get() const noexcept { return stream_; }

    // Stream Integrity Verification wrapper
    bool verify_integrity(std::string& out_error_message) const {
        char err_buf[512] = {0};
        int32_t ok = dgtl_stream_verify_integrity(stream_, err_buf, sizeof(err_buf));
        if (!ok) {
            out_error_message = err_buf;
            return false;
        }
        return true;
    }

    // Custom compiler pass invocation wrapper
    bool run_pass(const std::string& pass_name) {
        dgtl_status_t status = dgtl_run_pass_by_name(stream_, pass_name.c_str());
        return status.ok != 0;
    }

    // Binary Serialization wrapper
    std::vector<uint8_t> serialize() const {
        void* blob = nullptr;
        size_t size = 0;
        dgtl_status_t status = dgtl_stream_serialize(stream_, &blob, &size);
        if (!status.ok || !blob) return {};
        
        std::vector<uint8_t> result(static_cast<const uint8_t*>(blob), static_cast<const uint8_t*>(blob) + size);
        dgtl_free_serialized_blob(blob);
        return result;
    }

    // Binary Deserialization wrapper
    bool deserialize(const std::vector<uint8_t>& blob) {
        dgtl_status_t status = dgtl_stream_deserialize(stream_, blob.data(), blob.size());
        return status.ok != 0;
    }

    // CFG Introspection
    size_t get_block_count() const { return dgtl_stream_cfg_block_count(stream_); }
    
    bool get_block(size_t index, dgtl_cfg_block_t& out_block) const {
        return dgtl_stream_cfg_get_block(stream_, index, &out_block) != 0;
    }
};
```
========================================================
ALL SDK FILES UNIFIED INTO ONE FILE NAMED "dgtl.h" ONLY for the purpose of making available all the symbols and functions to the clients , but clients need to include as per their requirement. only one file because in actual implementation they are 6 .h files , but they have only one implementation .cpp file 


/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 * Component:   Unified Public SDK Master Specification Entry Point
 * Purpose:     Exposes the absolute, single-include C ABI surface for multi-target 
 *              compilation, low-level scalar ingestion hooks, advanced CFG introspection, 
 *              and SIMD execution profiles. Tenders all sub-headers completely obsolete.
 */

#ifndef DGTL_SDK_H
#define DGTL_SDK_H

#include <stddef.h>
#include <stdint.h>

/* ========================================================================
 * EXPORT / VISIBILITY CONTROL DECORATIONS
 * ======================================================================== */
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
  #define DGTL_API  /* Static library link-target layout — no decoration */
#endif

#if defined(_WIN32) || defined(_WIN64) || defined(__CYGWIN__)
  #define DGTL_CALL __cdecl
#else
  #define DGTL_CALL
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================
 * CONSTANTS, ENUMS & SYSTEM TARGET LAYOUTS
 * ======================================================================== */
#define DGTL_VERSION_MAJOR   1
#define DGTL_VERSION_MINOR   2
#define DGTL_VERSION_PATCH   0
#define DGTL_VERSION_STRING "1.2.0"
#define DGTL_CAPABILITY_SCHEMA_VERSION 1u

DGTL_API const char* DGTL_CALL dgtl_version_string(void);

/* Strict Opaque Handles Defending Boundary Isolation */
typedef struct dgtl_context  dgtl_context_t;
typedef struct dgtl_stream   dgtl_stream_t;
typedef struct dgtl_session  dgtl_session_t;
typedef struct dgtl_function dgtl_function_t;
typedef struct dgtl_runtime_context dgtl_runtime_context_t;

typedef uint16_t dgtl_op_t;
typedef uint32_t dgtl_value_t;

/* Core Target Runtime ISA Enums */
typedef enum dgtl_target {
    DGTL_TARGET_ARM64      = 0,
    DGTL_TARGET_X64        = 1,
    DGTL_TARGET_NASM       = 2,
    DGTL_TARGET_WASM       = 3,
    DGTL_TARGET_ELF_OBJ    = 4,
    DGTL_TARGET_STATIC_LIB = 5,
    DGTL_TARGET_COFF_OBJ   = 6,
    DGTL_TARGET_COFF_LIB   = 7,
    DGTL_TARGET_MACHO      = 8,
    DGTL_TARGET_COUNT      = 9
} dgtl_target_t;

DGTL_API const char* DGTL_CALL dgtl_target_name(dgtl_target_t target);
DGTL_API const char* DGTL_CALL dgtl_target_extension(dgtl_target_t target);

/* Stable Low-Level C ABI Instruction OpCodes */
typedef enum dgtl_opcode {
    DGTL_OP_NOP              = 0x00,
    DGTL_OP_CONST            = 0x01,
    DGTL_OP_ADD              = 0x02,
    DGTL_OP_SUB              = 0x03,
    DGTL_OP_MUL              = 0x04,
    DGTL_OP_DIV              = 0x05,
    DGTL_OP_MOD              = 0x06,
    DGTL_OP_AND              = 0x07,
    DGTL_OP_OR               = 0x08,
    DGTL_OP_XOR              = 0x09,
    DGTL_OP_SHL              = 0x0A,
    DGTL_OP_SHR              = 0x0B,
    DGTL_OP_NEG              = 0x0C,
    DGTL_OP_NOT              = 0x0D,
    DGTL_OP_LOAD             = 0x10,
    DGTL_OP_STORE            = 0x11,
    DGTL_OP_ATOMIC_LOAD      = 0x12,
    DGTL_OP_ATOMIC_STORE     = 0x13,
    DGTL_OP_ATOMIC_FENCE     = 0x14,
    DGTL_OP_PTR_CAST         = 0x15,
    DGTL_OP_PTR_DIFF         = 0x16,
    DGTL_OP_BOOL_SELECT      = 0x17,
    DGTL_OP_STRING_CONST     = 0x18,
    DGTL_OP_STRING_LENGTH    = 0x19,
    DGTL_OP_STRING_COMPARE   = 0x1A,
    DGTL_OP_STRING_CONCAT    = 0x1B,
    DGTL_OP_STRING_SUBSTRING = 0x1C,
    DGTL_OP_CALL             = 0x20,
    DGTL_OP_RET              = 0x21,
    DGTL_OP_LABEL            = 0x30,
    DGTL_OP_JMP              = 0x31,
    DGTL_OP_BR_IF            = 0x32,
    DGTL_OP_CMP_EQ           = 0x40,
    DGTL_OP_CMP_NE           = 0x41,
    DGTL_OP_CMP_LT           = 0x42,
    DGTL_OP_CMP_LE           = 0x43,
    DGTL_OP_CMP_GT           = 0x44,
    DGTL_OP_CMP_GE           = 0x45,
    DGTL_OP_FMA              = 0x50,
    DGTL_OP_FADD             = 0x51,
    DGTL_OP_FMUL             = 0x52,
    DGTL_OP_FSUB             = 0x53,
    DGTL_OP_FDIV             = 0x54,
    DGTL_OP_FADD_F64         = 0x55,
    DGTL_OP_FMUL_F64         = 0x56,
    DGTL_OP_FSUB_F64         = 0x57,
    DGTL_OP_FDIV_F64         = 0x58,
    DGTL_OP_PHI              = 0x60,
    DGTL_OP_GATHER           = 0x70,
    DGTL_OP_SCATTER          = 0x71,
    DGTL_OP_ADD_I64          = 0xA0,
    DGTL_OP_SUB_I64          = 0xA1,
    DGTL_OP_MUL_I64          = 0xA2,
    DGTL_OP_DIV_I64          = 0xA3,
    DGTL_OP_MOD_I64          = 0xA4,
    DGTL_OP_AND_I64          = 0xA5,
    DGTL_OP_OR_I64           = 0xA6,
    DGTL_OP_XOR_I64          = 0xA7,
    DGTL_OP_SHL_I64          = 0xA8,
    DGTL_OP_SHR_I64          = 0xA9,
    DGTL_OP_SELECT           = 0xB0,
    DGTL_OP_GEP              = 0xB1,
    DGTL_OP_PTR_TO_INT       = 0xB2,
    DGTL_OP_INT_TO_PTR       = 0xB3,
    DGTL_OP_READ_TSC         = 0xB4,
    DGTL_OP_BR_LIKELY        = 0xB5,
    DGTL_OP_BR_UNLIKELY      = 0xB6,
    DGTL_OP_PREFETCH_T0      = 0xB7,
    DGTL_OP_CLFLUSH          = 0xB8,
    DGTL_OP_SENTINEL         = 0xFF
} dgtl_opcode_t;

/* Dynamic Node Metadata Attribute Flags */
#define DGTL_FLAG_NONE              0x0000
#define DGTL_FLAG_IS_CONST         0x0001
#define DGTL_FLAG_IS_FP            0x0002
#define DGTL_FLAG_HAS_SIDE_EFFECT  0x0004
#define DGTL_FLAG_VOLATILE         0x0008
#define DGTL_FLAG_DEAD             0x0010
#define DGTL_FLAG_FMA_FUSED        0x0020
#define DGTL_FLAG_SPECULATED       0x0040
#define DGTL_FLAG_PREDICATED       0x0080

/* Optimization Strategy Pipeline Profiles */
typedef enum dgtl_opt_profile {
    DGTL_OPT_PROFILE_DEFAULT     = 0,
    DGTL_OPT_PROFILE_FOLD        = 1,
    DGTL_OPT_PROFILE_FUSION      = 2,
    DGTL_OPT_PROFILE_SIMD        = 3,
    DGTL_OPT_PROFILE_PRESSURE    = 4,
    DGTL_OPT_PROFILE_PREDICATION = 5
} dgtl_opt_profile_t;

/* Optimization Engine Operating Modes */
typedef enum dgtl_ir_mode {
    DGTL_IR_MODE_INT   = 0,
    DGTL_IR_MODE_FLOAT = 1,
    DGTL_IR_MODE_FMA   = 2
} dgtl_ir_mode_t;

/* Session Strategy Lifecycle Execution Modes */
typedef enum dgtl_session_mode {
    DGTL_SESSION_MODE_JIT    = 0,
    DGTL_SESSION_MODE_AOT    = 1,
    DGTL_SESSION_MODE_HYBRID = 2
} dgtl_session_mode_t;

typedef enum dgtl_session_opt_level {
    DGTL_SESSION_OPT_O0      = 0,
    DGTL_SESSION_OPT_O1      = 1,
    DGTL_SESSION_OPT_O2      = 2,
    DGTL_SESSION_OPT_O3      = 3,
    DGTL_SESSION_OPT_OSTRIKE = 4
} dgtl_session_opt_level_t;

typedef enum dgtl_host_symbol_flags {
    DGTL_HOST_SYMBOL_BIND_BY_NAME = 0x1,
    DGTL_HOST_SYMBOL_BIND_BY_SLOT = 0x2
} dgtl_host_symbol_flags_t;

/* ========================================================================
 * STRUCTURED DATA PLATFORMS & CONFIG MATRIX STRUCTS
 * ======================================================================== */
typedef struct dgtl_status {
    int  ok; 
    int  error_code;
    char message[256];
} dgtl_status_t;

typedef struct dgtl_strike_result {
    int      ok;
    size_t   code_size;
    uint64_t logic_hash;
    char     message[256];
} dgtl_strike_result_t;

typedef struct dgtl_node_info {
    dgtl_opcode_t opcode;
    uint32_t      slot;
    uint32_t      lhs;
    uint32_t      rhs;
    uint64_t      immediate;
    uint16_t      flags;
} dgtl_node_info_t;

typedef struct dgtl_perf_metrics {
    uint64_t total_compilation_time_us;
    uint64_t optimization_time_us;
    uint64_t emission_time_us;
    uint32_t peak_register_count;
    uint32_t peak_memory_usage_bytes;
    uint64_t estimated_cycle_count;
} dgtl_perf_metrics_t;

typedef enum dgtl_cfg_edge_kind {
    DGTL_CFG_EDGE_FALLTHROUGH       = 0,
    DGTL_CFG_EDGE_UNCONDITIONAL     = 1,
    DGTL_CFG_EDGE_CONDITIONAL_TRUE  = 2,
    DGTL_CFG_EDGE_CONDITIONAL_FALSE = 3
} dgtl_cfg_edge_kind_t;

typedef struct dgtl_cfg_block {
    uint32_t id;
    size_t begin;
    size_t end;
    uint32_t primary_label;
    int32_t is_entry;
} dgtl_cfg_block_t;

typedef struct dgtl_cfg_edge {
    uint32_t from;
    uint32_t to;
    dgtl_cfg_edge_kind_t kind;
} dgtl_cfg_edge_t;

typedef struct dgtl_capabilities {
    uint32_t schema_version;
    uint32_t supports_jit;
    uint32_t supports_aot;
    uint32_t supports_hybrid;
    uint32_t supports_host_symbol_name_binding;
    uint32_t supports_host_symbol_slot_binding;
    uint32_t supports_object_emission;
    uint32_t supports_binary_emission;
    uint32_t supports_in_process_execution;
    uint32_t supports_cfg_introspection;
    uint32_t supports_per_pass_control;
    uint32_t max_call_arity;
    uint64_t backend_target_mask;
    uint64_t optimization_profile_mask;
    uint32_t reserved[8];
} dgtl_capabilities_t;

typedef struct dgtl_session_config {
    dgtl_session_mode_t mode;
    dgtl_session_opt_level_t opt_level;
    uint64_t timeout_us;
    int enable_temporal_fusion;
    int enable_polyhedral_scop;
    int enable_speculation;
    int enable_vectorization;
    uint32_t thread_count;
    uint32_t reserved[8];
} dgtl_session_config_t;

typedef struct dgtl_host_symbol {
    const char* name;
    uint32_t slot;
    uintptr_t address;
    uint32_t flags;
} dgtl_host_symbol_t;

typedef struct dgtl_callee_binding {
    uint32_t    slot;
    const char* symbol_name;
} dgtl_callee_binding_t;

typedef struct dgtl_emit_options {
    int32_t inline_threshold;
    int32_t unroll_factor;
    int32_t enable_cache_optimization;
    int32_t enable_branch_prediction;
    int32_t vectorize_threshold;
    int32_t reserved[11];
} dgtl_emit_options_t;

typedef void (*dgtl_runtime_task_fn)(void* context);

/* ========================================================================
 * PUBLIC CORE ABI LIFE WORKFLOW METHODS
 * ======================================================================== */

/* Lifecycle Engines */
DGTL_API dgtl_context_t* DGTL_CALL dgtl_context_create(void);
DGTL_API void            DGTL_CALL dgtl_context_destroy(dgtl_context_t* ctx);
DGTL_API size_t          DGTL_CALL dgtl_context_memory_usage(const dgtl_context_t* ctx);

DGTL_API dgtl_stream_t*  DGTL_CALL dgtl_stream_create(dgtl_context_t* ctx, size_t reserved_nodes);
DGTL_API void            DGTL_CALL dgtl_stream_destroy(dgtl_stream_t* stream);
DGTL_API size_t          DGTL_CALL dgtl_stream_node_count(const dgtl_stream_t* stream);
DGTL_API uint64_t        DGTL_CALL dgtl_stream_logic_hash(const dgtl_stream_t* stream);
DGTL_API size_t          DGTL_CALL dgtl_stream_memory_usage(const dgtl_stream_t* s);
DGTL_API dgtl_status_t   DGTL_CALL dgtl_stream_reserve(dgtl_stream_t* s, size_t additional_nodes);
DGTL_API dgtl_status_t   DGTL_CALL dgtl_stream_shrink_to_fit(dgtl_stream_t* s);

/* Global Unlocked High-Velocity Ingestion Tunnel Hooks */
DGTL_API uint32_t DGTL_CALL dgtl_emit_op(dgtl_stream_t* s, dgtl_opcode_t op, uint32_t lhs, uint32_t rhs, uint64_t immediate, uint16_t flags);
DGTL_API uint32_t DGTL_CALL dgtl_emit_op_with_result(dgtl_stream_t* s, uint32_t result_slot, dgtl_opcode_t op, uint32_t lhs, uint32_t rhs, uint64_t immediate, uint16_t flags);

/* Standard Type-Explicit Immediates & Primitives */
DGTL_API uint32_t DGTL_CALL dgtl_emit_const_i64(dgtl_stream_t* s, uint64_t value);
DGTL_API uint32_t DGTL_CALL dgtl_emit_const_f32(dgtl_stream_t* s, float value);
DGTL_API uint32_t DGTL_CALL dgtl_emit_const_f64(dgtl_stream_t* s, double value);
DGTL_API uint32_t DGTL_CALL dgtl_emit_const_binary(dgtl_stream_t* s, const void* data, size_t len);

/* Target Operation Specific Emits */
DGTL_API uint32_t DGTL_CALL dgtl_emit_add_i32(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);
DGTL_API uint32_t DGTL_CALL dgtl_emit_sub_i32(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);
DGTL_API uint32_t DGTL_CALL dgtl_emit_mul_i32(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);
DGTL_API uint32_t DGTL_CALL dgtl_emit_div_i32(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);
DGTL_API uint32_t DGTL_CALL dgtl_emit_mod_i32(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);
DGTL_API uint32_t DGTL_CALL dgtl_emit_add_i64(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);
DGTL_API uint32_t DGTL_CALL dgtl_emit_sub_i64(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);
DGTL_API uint32_t DGTL_CALL dgtl_emit_mul_i64(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);
DGTL_API uint32_t DGTL_CALL dgtl_emit_div_i64(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);
DGTL_API uint32_t DGTL_CALL dgtl_emit_mod_i64(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);

DGTL_API uint32_t DGTL_CALL dgtl_emit_and_i32(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);
DGTL_API uint32_t DGTL_CALL dgtl_emit_or_i32 (dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);
DGTL_API uint32_t DGTL_CALL dgtl_emit_xor_i32(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);
DGTL_API uint32_t DGTL_CALL dgtl_emit_shl_i32(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);
DGTL_API uint32_t DGTL_CALL dgtl_emit_shr_i32(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);
DGTL_API uint32_t DGTL_CALL dgtl_emit_and_i64(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);
DGTL_API uint32_t DGTL_CALL dgtl_emit_or_i64 (dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);
DGTL_API uint32_t DGTL_CALL dgtl_emit_xor_i64(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);
DGTL_API uint32_t DGTL_CALL dgtl_emit_shl_i64(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);
DGTL_API uint32_t DGTL_CALL dgtl_emit_shr_i64(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);

/* String Management & Registry Hooks */
DGTL_API void     DGTL_CALL dgtl_emit_constant_str(dgtl_stream_t* s, dgtl_value_t dest_reg, const char* value, size_t length);
DGTL_API uint32_t DGTL_CALL dgtl_emit_string_const_handle(dgtl_stream_t* s, uint32_t string_handle);
DGTL_API uint32_t DGTL_CALL dgtl_emit_string_const_utf8(dgtl_stream_t* s, const char* utf8_literal);
DGTL_API uint32_t DGTL_CALL dgtl_emit_string_length(dgtl_stream_t* s, uint32_t string_slot);
DGTL_API uint32_t DGTL_CALL dgtl_emit_string_compare(dgtl_stream_t* s, uint32_t lhs_string_slot, uint32_t rhs_string_slot);
DGTL_API uint32_t DGTL_CALL dgtl_emit_string_concat(dgtl_stream_t* s, uint32_t lhs_string_slot, uint32_t rhs_string_slot);
DGTL_API uint32_t DGTL_CALL dgtl_emit_string_substring(dgtl_stream_t* s, uint32_t string_slot, uint32_t start_slot, uint32_t length_slot);

/* Terminal Nodes, Control Flow & Binding Operators */
DGTL_API uint32_t DGTL_CALL dgtl_emit_phi(dgtl_stream_t* s, uint32_t* incoming_slots, size_t incoming_count);
DGTL_API uint32_t DGTL_CALL dgtl_emit_label(dgtl_stream_t* s, const char* label_name);
DGTL_API uint32_t DGTL_CALL dgtl_emit_label_slot(dgtl_stream_t* s, uint32_t label_slot);
DGTL_API uint32_t DGTL_CALL dgtl_emit_function_label_slot(dgtl_stream_t* s, uint32_t function_slot);
DGTL_API void     DGTL_CALL dgtl_emit_return(dgtl_stream_t* s, uint32_t value_slot);

DGTL_API void     DGTL_CALL dgtl_emit_branch(dgtl_stream_t* s, uint32_t target_label);
DGTL_API void     DGTL_CALL dgtl_emit_branch_cond(dgtl_stream_t* s, uint32_t condition, uint32_t target_label);
DGTL_API void     DGTL_CALL dgtl_emit_branch_predictive(dgtl_stream_t* s, dgtl_value_t condition_reg, uint32_t true_label_id, uint32_t false_label_id, uint8_t runtime_weight);
DGTL_API void     DGTL_CALL dgtl_emit_hardware_affinity(dgtl_stream_t* s, dgtl_value_t mask_register_id);

DGTL_API uint32_t DGTL_CALL dgtl_emit_call(dgtl_stream_t* s, uint32_t callee_slot, const uint32_t* args, size_t arg_count);
DGTL_API uint32_t DGTL_CALL dgtl_emit_call_with_result(dgtl_stream_t* s, uint32_t result_slot, uint32_t callee_slot, const uint32_t* args, size_t arg_count);
DGTL_API uint32_t DGTL_CALL dgtl_emit_call_variadic(dgtl_stream_t* s, uint32_t callee_slot, const uint32_t* args, size_t arg_count);
DGTL_API uint32_t DGTL_CALL dgtl_emit_call_variadic_with_result(dgtl_stream_t* s, uint32_t result_slot, uint32_t callee_slot, const uint32_t* args, size_t arg_count);

DGTL_API dgtl_status_t DGTL_CALL dgtl_stream_bind_callee_slot(dgtl_stream_t* stream, uint32_t slot, const char* symbol_name);
DGTL_API dgtl_status_t DGTL_CALL dgtl_stream_bind_callee_slots(dgtl_stream_t* stream, const dgtl_callee_binding_t* bindings, size_t binding_count);

/* Advanced Inspection Matrix & Analysis passes */
DGTL_API dgtl_status_t DGTL_CALL dgtl_set_ir_mode(dgtl_stream_t* s, dgtl_ir_mode_t mode);
DGTL_API dgtl_status_t DGTL_CALL dgtl_optimize(dgtl_stream_t* s);
DGTL_API dgtl_status_t DGTL_CALL dgtl_optimize_with_profile(dgtl_stream_t* s, dgtl_opt_profile_t profile);
DGTL_API dgtl_status_t DGTL_CALL dgtl_stream_compact(dgtl_stream_t* s);
DGTL_API int32_t       DGTL_CALL dgtl_stream_verify_integrity(const dgtl_stream_t* s, char* error_buffer, size_t error_len);

DGTL_API int32_t DGTL_CALL dgtl_stream_get_perf_metrics(const dgtl_stream_t* s, dgtl_perf_metrics_t* out);
DGTL_API size_t  DGTL_CALL dgtl_stream_cfg_block_count(const dgtl_stream_t* s);
DGTL_API size_t  DGTL_CALL dgtl_stream_cfg_edge_count(const dgtl_stream_t* s);
DGTL_API int32_t DGTL_CALL dgtl_stream_cfg_get_block(const dgtl_stream_t* s, size_t index, dgtl_cfg_block_t* out);
DGTL_API int32_t DGTL_CALL dgtl_stream_cfg_get_edge(const dgtl_stream_t* s, size_t index, dgtl_cfg_edge_t* out);
DGTL_API size_t  DGTL_CALL dgtl_stream_cfg_get_successors(const dgtl_stream_t* s, uint32_t block_id, uint32_t* out_ids, size_t out_cap);
DGTL_API size_t  DGTL_CALL dgtl_stream_cfg_get_predecessors(const dgtl_stream_t* s, uint32_t block_id, uint32_t* out_ids, size_t out_cap);
DGTL_API int     DGTL_CALL dgtl_stream_get_node(const dgtl_stream_t* s, size_t index, dgtl_node_info_t* out);

/* Optimization Manager Pass Pipelines */
DGTL_API dgtl_status_t DGTL_CALL dgtl_run_pass_by_name(dgtl_stream_t* s, const char* pass_name);
DGTL_API int32_t       DGTL_CALL dgtl_has_pass(const char* pass_name);
DGTL_API const char**  DGTL_CALL dgtl_list_passes(size_t* out_count);

DGTL_API dgtl_status_t DGTL_CALL dgtl_set_optimization_enabled(dgtl_stream_t* s, int32_t enabled);
DGTL_API int32_t       DGTL_CALL dgtl_get_optimization_level(const dgtl_stream_t* s);
DGTL_API dgtl_status_t DGTL_CALL dgtl_set_optimization_level(dgtl_stream_t* s, int32_t level);

/* Target Driver Engine Registries */
DGTL_API dgtl_capabilities_t DGTL_CALL dgtl_get_capabilities(void);
DGTL_API int                 DGTL_CALL dgtl_capabilities_support_target(const dgtl_capabilities_t* caps, dgtl_target_t target);

DGTL_API dgtl_session_config_t DGTL_CALL dgtl_session_config_default(void);
DGTL_API const char*           DGTL_CALL dgtl_session_mode_name(dgtl_session_mode_t mode);
DGTL_API dgtl_session_t*       DGTL_CALL dgtl_session_create(const dgtl_session_config_t* config);
DGTL_API void                  DGTL_CALL dgtl_session_destroy(dgtl_session_t* session);

DGTL_API dgtl_status_t DGTL_CALL dgtl_session_register_host_symbol(dgtl_session_t* session, const char* symbol_name, uintptr_t address);
DGTL_API dgtl_status_t DGTL_CALL dgtl_session_register_host_symbol_slot(dgtl_session_t* session, uint32_t slot, uintptr_t address);
DGTL_API dgtl_status_t DGTL_CALL dgtl_session_register_host_symbols(dgtl_session_t* session, const dgtl_host_symbol_t* symbols, size_t symbol_count);

DGTL_API dgtl_status_t DGTL_CALL dgtl_session_compile_stream(dgtl_session_t* session, dgtl_stream_t* stream);
DGTL_API uint64_t      DGTL_CALL dgtl_session_get_symbol_address(dgtl_session_t* session, const char* symbol_name);
DGTL_API dgtl_status_t DGTL_CALL dgtl_session_emit_object(dgtl_session_t* session, const char* output_path);
DGTL_API dgtl_status_t DGTL_CALL dgtl_session_emit_binary(dgtl_session_t* session, const char* output_path);
DGTL_API dgtl_status_t DGTL_CALL dgtl_session_run_entry_i64(dgtl_session_t* session, const char* entry_symbol, int64_t* out_result);

DGTL_API dgtl_strike_result_t DGTL_CALL dgtl_strike_stream(dgtl_stream_t* s, dgtl_target_t target);
DGTL_API dgtl_strike_result_t DGTL_CALL dgtl_strike_stream_with_options(dgtl_stream_t* s, dgtl_target_t target, const dgtl_emit_options_t* options);
DGTL_API dgtl_emit_options_t   DGTL_CALL dgtl_emit_options_default(dgtl_target_t target);
DGTL_API size_t               DGTL_CALL dgtl_estimate_code_size(const dgtl_stream_t* s, dgtl_target_t target);
DGTL_API dgtl_status_t        DGTL_CALL dgtl_emit_to_file(dgtl_stream_t* s, dgtl_target_t target, const char* output_path);

/* Serialization Interchange Packets */
DGTL_API dgtl_status_t DGTL_CALL dgtl_stream_serialize(const dgtl_stream_t* s, void** out_blob, size_t* out_size);
DGTL_API dgtl_status_t DGTL_CALL dgtl_stream_deserialize(dgtl_stream_t* s, const void* blob, size_t blob_size);
DGTL_API void          DGTL_CALL dgtl_free_serialized_blob(void* blob);

/* Internal Analytics, Logs & Diagnostics */
DGTL_API void        DGTL_CALL dgtl_stream_dump_ir(const dgtl_stream_t* s, void* custom_file_handle_opt);
DGTL_API const char* DGTL_CALL dgtl_get_last_error(void);
DGTL_API void        DGTL_CALL dgtl_clear_error(void);
DGTL_API void        DGTL_CALL dgtl_set_verbose(int32_t enabled);
DGTL_API const char* DGTL_CALL dgtl_get_build_config(void);

DGTL_API size_t      DGTL_CALL dgtl_last_linker_fallback_count(void);
DGTL_API size_t      DGTL_CALL dgtl_last_linker_fallback_message(size_t index, char* out_buffer, size_t out_buffer_size);

/* Multi-Threaded task runner scheduler context */
DGTL_API dgtl_runtime_context_t* DGTL_CALL dgtl_runtime_context_create(size_t worker_threads, size_t arena_mb_per_worker);
DGTL_API void                    DGTL_CALL dgtl_runtime_context_destroy(dgtl_runtime_context_t* ctx);
DGTL_API uint64_t                DGTL_CALL dgtl_runtime_spawn_task(dgtl_runtime_context_t* ctx, dgtl_runtime_task_fn fn, void* arg);
DGTL_API void                    DGTL_CALL dgtl_runtime_join_task(dgtl_runtime_context_t* ctx, uint64_t task_handle);
DGTL_API void                    DGTL_CALL dgtl_runtime_yield(dgtl_runtime_context_t* ctx);

DGTL_API void                    DGTL_CALL dgtl_yield(void);
DGTL_API uint64_t                DGTL_CALL dgtl_spawn_task(dgtl_runtime_task_fn fn, void* arg);
DGTL_API void                    DGTL_CALL dgtl_join_task(uint64_t task_handle);

/* Hardware Accelerated ISEL Vectorization SIMD Hooks */
DGTL_API uint32_t      DGTL_CALL dgtl_emit_fma(dgtl_stream_t* s, uint32_t a, uint32_t b, uint32_t c);
DGTL_API uint32_t      DGTL_CALL dgtl_emit_gather(dgtl_stream_t* s, uint32_t base_slot, uint32_t index_slot, uint16_t flags);
DGTL_API uint32_t      DGTL_CALL dgtl_emit_scatter(dgtl_stream_t* s, uint32_t base_slot, uint32_t index_slot, uint32_t value_slot, uint16_t flags);
DGTL_API uint32_t      DGTL_CALL dgtl_emit_vec_add_f32_512(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);
DGTL_API uint32_t      DGTL_CALL dgtl_emit_vec_mul_f32_512(dgtl_stream_t* s, uint32_t lhs, uint32_t rhs);
DGTL_API uint32_t      DGTL_CALL dgtl_emit_store_v512(dgtl_stream_t* s, uint32_t addr_id, uint32_t val_id);
DGTL_API dgtl_status_t DGTL_CALL dgtl_legalize_simd(dgtl_stream_t* s, dgtl_simd_arch_t arch);

#ifdef __cplusplus
}
#endif




/* ---------- Contract Policy Verification ---------- */
#include "dgtl_core.h"
#include ""
#include "dgtl_capabilities.h"
#include "dgtl_session.h"
#include ""

#endif /* DGTL_SDK_H */