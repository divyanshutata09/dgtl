# DGTL Sovereign Compiler & Runtime SDK: Client Integration Guide (v2.1.0-SOVEREIGN)

> **Target Audience:** Language frontend authors, JIT engine implementers, systems programmers, and compiler architects integrating DGTL as a sovereign, high-performance compiler backend and JIT runtime.  
> **Standards:** C17 / C++20 / C++26 Compatible C-ABI  
> **Distribution:** Standalone Static Library (`dgtl.lib`), Modular C Headers (`include/dgtl_sdk/`), and Developer Discovery Tool (`dgtl-config.exe`).

---

## 1. Executive Overview & Architectural Philosophy

The **Deep General-purpose Transformation Layer (DGTL)** is a sovereign, language-agnostic compiler infrastructure and JIT execution engine. Architected under the design principles of **LLVM** (`llvm-c/`, `llvm-config`, component-based static libraries) and **Cranelift** (`cranelift-codegen`, `cranelift-jit`, `cranelift-object`, `cranelift-module`), DGTL provides a complete, modern compilation pipeline:

$$\text{Frontend AST} \longrightarrow \text{DGTL IR} \xrightarrow{\text{DCE / GVN}} \text{MIR} \xrightarrow{\text{ISel / RegAlloc}} \text{ISA (x64 MC)} \xrightarrow{\text{ObjectFile}} \begin{cases} \text{JIT Loader (W\^{}X Execution)} \\ \text{Sovereign Linker (Native PE32+ .exe)} \end{cases}$$

### Key Architectural Pillars
1. **Zero External Toolchain Dependencies:** DGTL includes an embedded, high-performance PE32+ linker and object emitter. Generating standalone native executables requires no external `link.exe`, `ld`, or MSVC toolchains.
2. **C-ABI Encapsulation:** The public API (`dgtl_sdk`) is strictly isolated behind standard C-linkage headers (`extern "C"`). Clients never depend on internal C++26 named modules (`import dgtl.*`), internal namespaces (`dgtl::core`, `dgtl::mir`), or compiler runtime internals.
3. **Dual Ergonomics:**
   - **C17 Pure C-ABI:** Provides raw, opaque handle-based functions for language frontends and FFI consumers (Rust, Python, Zig, Go, C#).
   - **C++20 RAII Facade:** Zero-overhead RAII wrappers (`dgtl::TargetMachine`, `dgtl::ObjectFile`, `dgtl::Linker`, `dgtl::JITEngine`, `dgtl::Session`, `dgtl::Stream`) delivering move semantics, deterministic destructor cleanup, and modern C++ ergonomics.
4. **W^X Memory Safety:** The JIT engine allocates memory with `PAGE_READWRITE` permissions during code loading and relocation resolution, enforces `PAGE_EXECUTE_READ` before execution, and executes hardware instruction cache flushes (`FlushInstructionCache`).

---

## 2. SDK Distribution Layout & Build Discovery

### A. Directory Hierarchy
The packaged DGTL SDK client distribution is organized as follows:

```text
DGTL_SDK/
├── bin/
│   └── dgtl-config.exe              <-- Configuration & Component Discovery Tool
├── include/
│   └── dgtl_sdk/
│       ├── types.h                  <-- Handles, status codes (dgtl_err_t), diagnostics
│       ├── target.h                 <-- Target machine, CPU features, triples, ABI
│       ├── object.h                 <-- Universal object file synthesis (PE, ELF, WASM)
│       ├── linker.h                 <-- In-memory & on-disk linking (PE32+, shared lib)
│       ├── jit.h                    <-- High-performance JIT execution & host resolver
│       ├── module.h                 <-- Multi-function module compilation
│       ├── mir.h                    <-- Machine IR disassembly & inspection
│       └── dgtl.h                   <-- Master umbrella header & C++20 RAII facade
└── lib/
    ├── cmake/
    │   └── DGTL/
    │       └── DGTLConfig.cmake     <-- CMake Package Config (find_package(DGTL))
    ├── pkgconfig/
    │   └── dgtl.pc                  <-- Pkg-config metadata
    └── x64/
        ├── dgtl.lib                 <-- Sovereign Static Library (~60 MB self-contained)
        └── COMPONENT_MANIFEST.json  <-- Formal JSON component manifest
```

---

### B. Automated Discovery with `dgtl-config`
Modeled directly after `llvm-config`, `dgtl-config.exe` allows build scripts, Makefiles, and external build engines to inspect SDK paths and flags dynamically:

```bash
# Print installed SDK version
dgtl-config --version
# Output: 2.1.0-SOVEREIGN

# Compiler flags required for C compilation
dgtl-config --cflags
# Output: -I"D:/TataG/DGTL/build/DGTL_SDK/include"

# Compiler flags required for C++20/C++26 compilation
dgtl-config --cxxflags
# Output: -std=c++26 -I"D:/TataG/DGTL/build/DGTL_SDK/include"

# Linker library search path
dgtl-config --ldflags
# Output: -L"D:/TataG/DGTL/build/DGTL_SDK/lib/x64"

# Link against unified library or specific components
dgtl-config --libs
# Output: -ldgtl

# Full path to static library file
dgtl-config --libfiles
# Output: D:/TataG/DGTL/build/DGTL_SDK/lib/x64/dgtl.lib

# List available internal components
dgtl-config --components
# Output: core ir opt mir isa codegen object jit linker runtime sdk all
```

---

### C. Build System Integrations

#### 1. CMake Integration
Add the SDK `lib/cmake/DGTL` to your `CMAKE_PREFIX_PATH`:

```cmake
cmake_minimum_required(VERSION 3.20)
project(MyLanguageCompiler LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(DGTL REQUIRED)

add_executable(my_compiler main.cpp)
target_link_libraries(my_compiler PRIVATE DGTL::dgtl kernel32 ntdll ws2_32)
```

#### 2. Pkg-Config Integration
```makefile
DGTL_CFLAGS  := $(shell pkg-config --cflags dgtl)
DGTL_LDFLAGS := $(shell pkg-config --libs dgtl) -lkernel32 -lntdll -lws2_32

my_compiler: main.o
	$(CXX) main.o $(DGTL_LDFLAGS) -o $@

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(DGTL_CFLAGS) -c $< -o $@
```

#### 3. Direct GCC / Clang Command Line
```bash
g++ -std=c++20 -O2 \
    -I"D:/TataG/DGTL/build/DGTL_SDK/include" \
    client.cpp \
    -L"D:/TataG/DGTL/build/DGTL_SDK/lib/x64" -ldgtl \
    -lkernel32 -lntdll -lws2_32 -Wl,--allow-multiple-definition \
    -o client.exe
```

---

## 3. Core ABI, Lifecycles & Error Handling Model

All DGTL SDK functions use the C calling convention (`DGTL_CALL` = `__cdecl` on Windows) and are declared `noexcept` (`DGTL_NOEXCEPT`).

### A. Opaque Handle Architecture
Clients interact exclusively through pointer handles:

| Opaque Handle | Header | Description |
|---|---|---|
| `dgtl_context_t*` | `types.h` | Shared arena memory pool and string interning table |
| `dgtl_target_machine_t` | `target.h` | Target architecture and hardware capabilities descriptor |
| `dgtl_stream_t*` | `dgtl.h` | Intermediate Representation (IR) node stream |
| `dgtl_func_builder_t*` | `dgtl.h` | High-level function builder with SSA block graphs |
| `dgtl_object_t` | `object.h` | In-memory binary object file with sections and symbols |
| `dgtl_linker_t` | `linker.h` | In-memory standalone linker driver |
| `dgtl_jit_engine_t` | `jit.h` | High-performance W^X execution engine |
| `dgtl_module_t` | `module.h` | Multi-function module container |
| `dgtl_session_t*` | `dgtl.h` | High-level unified compilation and JIT session |

---

### B. Ownership & Memory Lifecycle Rules
Every `dgtl_*_create()` function has a corresponding `dgtl_*_destroy()` function.

1. **Context Lifespan:** A `dgtl_context_t` manages memory arenas. Any streams or JIT engines associated with a context must be destroyed **before** the context is destroyed.
2. **Stream Independence:** An IR stream can be destroyed once compiled into an object or loaded into a JIT engine.
3. **Thread Safety:**
   - `dgtl_context_t`: **Thread-safe**. Multiple worker threads can allocate streams from a single context.
   - `dgtl_stream_t*`: **Not thread-safe**. Must be manipulated by exactly one thread at a time.
   - `dgtl_jit_engine_t`: **Thread-safe for lookup and execution** after compilation completes.

---

### C. Error Codes & Diagnostic Records
DGTL uses a lightweight value-based error struct `dgtl_err_t`:

```c
typedef enum dgtl_severity {
    DGTL_SEV_INFO    = 0,
    DGTL_SEV_WARNING = 1,
    DGTL_SEV_ERROR   = 2,
    DGTL_SEV_FATAL   = 3
} dgtl_severity_t;

typedef struct dgtl_err {
    int32_t         code;      /* 0 == Success */
    dgtl_severity_t severity;
} dgtl_err_t;

#define DGTL_IS_OK(e)   ((e).code == 0)
#define DGTL_IS_ERR(e)  ((e).code != 0)
```

#### Diagnostic Error Query Pattern
```c
dgtl_err_t err = dgtl_session_compile(session, stream);
if (DGTL_IS_ERR(err)) {
    const char* message = dgtl_get_last_error();
    fprintf(stderr, "[DGTL Error %d] %s\n", err.code, message);
}
```

---

## 4. Target Machine Subsystem (`<dgtl_sdk/target.h>`)

The Target Machine subsystem encapsulates the CPU architecture, instruction set extensions, data layout, relocations, and ABI rules.

### A. Creating Target Machines
```c
#include <dgtl_sdk/target.h>

// 1. Automatically detect host machine configuration
dgtl_target_machine_t host_tm = dgtl_target_machine_create_host();

// 2. Or configure a specific cross-compilation target
dgtl_target_machine_t custom_tm = dgtl_target_machine_create(
    "x86_64-pc-windows-msvc",     /* Target triple */
    "znver4",                     /* CPU microarchitecture */
    "+avx512f,+avx512dq",         /* Target feature flags */
    DGTL_OPT_O3,                  /* Optimization level */
    DGTL_RELOC_PIC,               /* Relocation model */
    DGTL_CODE_MODEL_SMALL         /* Code model */
);

// Query architecture attributes
const char* triple = dgtl_target_machine_get_triple(host_tm);
const char* cpu    = dgtl_target_machine_get_cpu(host_tm);
bool has_avx2      = dgtl_target_machine_has_feature(host_tm, "avx2");
size_t ptr_size    = dgtl_target_machine_get_pointer_size(host_tm); // 8 bytes
bool is_64bit      = dgtl_target_machine_is_64bit(host_tm);        // true

dgtl_target_machine_destroy(host_tm);
dgtl_target_machine_destroy(custom_tm);
```

---

## 5. Universal Object File Subsystem (`<dgtl_sdk/object.h>`)

The Object File subsystem allows clients to synthesize, inspect, and serialize relocatable object files across multiple file formats (COFF/PE, ELF, Mach-O, WASM).

### A. In-Memory Object Synthesis Recipe
```c
#include <dgtl_sdk/object.h>

// Create a new COFF/PE object
dgtl_object_t obj = dgtl_object_create(DGTL_OBJECT_FMT_PE);

// Add .text code section (16-byte aligned, Executable, Code, Readable)
dgtl_section_t text_sec = dgtl_object_add_section(
    obj,
    ".text",
    16,
    DGTL_SEC_FLAG_CODE | DGTL_SEC_FLAG_EXECUTE | DGTL_SEC_FLAG_READ
);

// Machine code: xor eax, eax; ret (48 31 C0 C3)
const uint8_t code[] = { 0x48, 0x31, 0xC0, 0xC3 };
dgtl_section_append_data(text_sec, code, sizeof(code));

// Add global function symbol
dgtl_symbol_t sym = dgtl_object_add_symbol(
    obj,
    "my_zero_func",
    0,                  /* Offset in section */
    sizeof(code),       /* Symbol size */
    0,                  /* Section index (0 for first section) */
    DGTL_SYM_BIND_GLOBAL,
    DGTL_SYM_TYPE_FUNC
);

// Emit directly to disk as relocatable object file (.obj / .o)
dgtl_object_emit_to_file(obj, "output.obj");

dgtl_object_destroy(obj);
```

---

## 6. Sovereign Linker Subsystem (`<dgtl_sdk/linker.h>`)

DGTL includes an embedded, high-performance binary linker capable of emitting standalone PE32+ executables without invoking external system linkers.

### B. Standalone Native Executable Emission
```c
#include <dgtl_sdk/linker.h>

dgtl_linker_t linker = dgtl_linker_create(DGTL_OBJECT_FMT_PE);

// Configure executable parameters
dgtl_linker_set_entry_point(linker, "main");
dgtl_linker_set_subsystem(linker, DGTL_SUBSYSTEM_CONSOLE);
dgtl_linker_set_image_base(linker, 0x140000000);

// Ingest object files and static archives
dgtl_linker_add_object_file(linker, "output.obj");
dgtl_linker_add_library_search_path(linker, "D:/TataG/MYZON/build");
dgtl_linker_add_archive(linker, "libmyzon_rt.a");

// Link directly to standalone executable
dgtl_err_t err = dgtl_linker_link_to_file(linker, DGTL_LINK_EXECUTABLE, "program.exe");
if (DGTL_IS_OK(err)) {
    printf("[Linker] Standalone PE executable written to program.exe\n");
}

dgtl_linker_destroy(linker);
```

---

## 7. Advanced JIT Execution Subsystem (`<dgtl_sdk/jit.h>`)

The JIT engine delivers high-throughput in-memory execution with full W^X memory protection and host symbol binding.

### A. JIT Execution Recipe with Host Symbol Resolution
```c
#include <dgtl_sdk/dgtl.h>
#include <dgtl_sdk/jit.h>
#include <stdio.h>

// Dynamic host resolver callback for external C runtime symbols
static uintptr_t my_symbol_resolver(const char* name, void* user_data) {
    if (strcmp(name, "printf") == 0)    return (uintptr_t)&printf;
    if (strcmp(name, "malloc") == 0)    return (uintptr_t)&malloc;
    if (strcmp(name, "free") == 0)      return (uintptr_t)&free;
    return 0; // Symbol unresolved
}

int main() {
    dgtl_context_t* ctx = dgtl_context_create(4, 64);
    dgtl_target_machine_t tm = dgtl_target_machine_create_host();
    
    // Create JIT Engine
    dgtl_jit_engine_t jit = dgtl_jit_engine_create(ctx, tm);
    dgtl_jit_engine_set_symbol_resolver(jit, my_symbol_resolver, NULL);

    // Build IR Stream
    dgtl_stream_t* s = dgtl_stream_create(ctx, 256);
    dgtl_func_builder_t* fb = dgtl_func_builder_create(s, "add_numbers", DGTL_TYPE_I64);
    
    dgtl_value_t a = dgtl_func_add_param(fb, DGTL_TYPE_I64);
    dgtl_value_t b = dgtl_func_add_param(fb, DGTL_TYPE_I64);
    dgtl_block_t entry = dgtl_func_append_block(fb, "entry");
    dgtl_func_set_insert_block(fb, entry.id);

    dgtl_value_t sum = dgtl_build_add(fb, a, b);
    dgtl_build_ret(fb, sum);
    dgtl_func_builder_finalize(fb);

    // Load Stream into JIT (Optimizes, selects machine instructions, enforces W^X)
    dgtl_err_t err = dgtl_jit_engine_add_stream(jit, s, "add_numbers");
    if (DGTL_IS_OK(err)) {
        // Resolve function pointer
        typedef int64_t (*AddFn)(int64_t, int64_t);
        AddFn add_ptr = (AddFn)dgtl_jit_engine_lookup(jit, "add_numbers");
        
        int64_t result = add_ptr(42, 58);
        printf("JIT Execution Result: 42 + 58 = %lld\n", result); // Prints 100
    }

    // Cleanup
    dgtl_stream_destroy(s);
    dgtl_func_builder_destroy(fb);
    dgtl_jit_engine_destroy(jit);
    dgtl_target_machine_destroy(tm);
    dgtl_context_destroy(ctx);
    return 0;
}
```

---

## 8. Multi-Function Module Container (`<dgtl_sdk/module.h>`)

For whole-program compilation where multiple functions share global state and reference one another:

```c
#include <dgtl_sdk/module.h>

dgtl_module_t mod = dgtl_module_create(ctx, "math_module");

// Register global read-only string constant
const char msg[] = "Hello from DGTL Sovereign SDK!\n";
dgtl_module_add_global_data(
    mod,
    "g_hello_msg",
    (const uint8_t*)msg,
    sizeof(msg),
    16,                  /* Alignment */
    true,                /* Constant */
    DGTL_LINKAGE_EXTERNAL
);

// Add functions
dgtl_module_add_function(mod, "func_a", stream_a, DGTL_LINKAGE_EXTERNAL);
dgtl_module_add_function(mod, "func_b", stream_b, DGTL_LINKAGE_INTERNAL);

// Compile entire module directly to relocatable object file
dgtl_object_t compiled_obj = dgtl_module_compile_to_object(mod, tm);
dgtl_object_emit_to_file(compiled_obj, "math_module.obj");

dgtl_object_destroy(compiled_obj);
dgtl_module_destroy(mod);
```

---

## 9. Machine IR (MIR) & Disassembly (`<dgtl_sdk/mir.h>`)

Inspect the generated low-level target machine code before emission:

```c
#include <dgtl_sdk/mir.h>

dgtl_text_t disasm{};
dgtl_err_t err = dgtl_mir_dump_machine_ir(stream, tm, &disasm);
if (DGTL_IS_OK(err)) {
    printf("--- Machine IR Disassembly ---\n%s\n", disasm.data);
    dgtl_free_text(disasm);
}
```

---

## 10. Intermediate Representation (IR) & Function Builder (`<dgtl_sdk/dgtl.h>`)

### Core Types & Immediate Constants
DGTL supports high-performance typed registers:
* `DGTL_TYPE_VOID` (0)
* `DGTL_TYPE_I1` (1), `DGTL_TYPE_I8` (2), `DGTL_TYPE_I16` (3), `DGTL_TYPE_I32` (4), `DGTL_TYPE_I64` (5)
* `DGTL_TYPE_F32` (6), `DGTL_TYPE_F64` (7)
* `DGTL_TYPE_PTR` (8)

### Control Flow & Phi Nodes
```c
dgtl_block_t b_then = dgtl_func_append_block(fb, "then_block");
dgtl_block_t b_else = dgtl_func_append_block(fb, "else_block");
dgtl_block_t b_merge = dgtl_func_append_block(fb, "merge_block");

dgtl_value_t cond = dgtl_build_icmp(fb, DGTL_ICMP_SGT, val_x, val_y);
dgtl_build_cond_br(fb, cond, b_then, b_else);

// Then block
dgtl_func_set_insert_block(fb, b_then.id);
dgtl_value_t r_then = dgtl_build_mul(fb, val_x, val_x);
dgtl_build_br(fb, b_merge);

// Else block
dgtl_func_set_insert_block(fb, b_else.id);
dgtl_value_t r_else = dgtl_build_add(fb, val_y, val_y);
dgtl_build_br(fb, b_merge);

// Merge block with SSA Phi node
dgtl_func_set_insert_block(fb, b_merge.id);
dgtl_value_t phi = dgtl_build_phi(fb, DGTL_TYPE_I64);
dgtl_phi_add_incoming(fb, phi, r_then, b_then);
dgtl_phi_add_incoming(fb, phi, r_else, b_else);
dgtl_build_ret(fb, phi);
```

### Frontend Lowering Calls (`dgtl_emit_call`)
For frontend language compilers emitting calls with arbitrary arguments:
```c
uint32_t call_args[4] = { arg0.id, arg1.id, arg2.id, arg3.id };
dgtl_emit_call(
    stream,
    callee_slot,       /* Register containing function pointer or symbol slot */
    result_slot,       /* Output register slot */
    call_args,         /* Arguments array */
    4                  /* Argument count */
);
```

---

## 11. Zero-Overhead C++20 RAII Facade

For modern C++ applications, include `<dgtl_sdk/dgtl.h>`. The `dgtl::*` classes manage all lifetimes automatically:

```cpp
#include <iostream>
#include <dgtl_sdk/dgtl.h>

int main() {
    // 1. RAII Context & Target Machine
    dgtl::Context ctx(4, 64);
    dgtl::TargetMachine tm("x86_64-pc-windows-msvc", "generic");

    // 2. RAII Stream & Function Builder
    dgtl::Stream stream(ctx);
    dgtl::FuncBuilder fb(stream, "compute", DGTL_TYPE_I64);
    
    auto a = fb.param(DGTL_TYPE_I64);
    auto b = fb.param(DGTL_TYPE_I64);
    auto bb = fb.append_block("entry");
    fb.set_insert_block(bb);

    auto sum = fb.add(a, b);
    auto prod = fb.mul(sum, fb.k_i64(2));
    fb.ret(prod);
    fb.finalize();

    // 3. RAII JIT Engine
    dgtl::JITEngine jit(ctx, tm);
    jit.add_stream(stream, "compute");

    auto* compute_fn = jit.get_function<int64_t(int64_t, int64_t)>("compute");
    std::cout << "(10 + 20) * 2 = " << compute_fn(10, 20) << std::endl; // Prints 60

    // 4. RAII Linker & Standalone Executable
    dgtl::Linker linker(DGTL_SUBSYSTEM_CONSOLE);
    linker.set_entry_point("main");
    
    // Everything is cleanly and automatically destroyed in reverse order of declaration!
    return 0;
}
```

---

## 12. Cross-Language FFI Recipes

### A. Rust FFI Integration
Create `build.rs`:
```rust
fn main() {
    let dgtl_lib = std::process::Command::new("dgtl-config")
        .arg("--libdir")
        .output()
        .expect("dgtl-config not found");
    let lib_path = String::from_utf8(dgtl_lib.stdout).unwrap().trim().to_string();
    println!("cargo:rustc-link-search=native={}", lib_path);
    println!("cargo:rustc-link-lib=static=dgtl");
    println!("cargo:rustc-link-lib=kernel32");
    println!("cargo:rustc-link-lib=ntdll");
    println!("cargo:rustc-link-lib=ws2_32");
}
```

And `src/main.rs`:
```rust
use std::ffi::{c_char, c_void, CStr, CString};

#[repr(C)]
#[derive(Copy, Clone, Debug)]
pub struct DgtlErr {
    pub code: i32,
    pub severity: u32,
}

#[link(name = "dgtl")]
extern "C" {
    fn dgtl_target_machine_create_host() -> *mut c_void;
    fn dgtl_target_machine_get_triple(tm: *mut c_void) -> *const c_char;
    fn dgtl_target_machine_destroy(tm: *mut c_void);
}

fn main() {
    unsafe {
        let tm = dgtl_target_machine_create_host();
        let triple_c = dgtl_target_machine_get_triple(tm);
        let triple = CStr::from_ptr(triple_c).to_string_lossy();
        println!("DGTL Host Target Triple: {}", triple);
        dgtl_target_machine_destroy(tm);
    }
}
```

---

### B. Python (`ctypes`) FFI Integration
```python
import ctypes
import subprocess

# Discover libraries using dgtl-config
lib_dir = subprocess.check_output(["dgtl-config", "--libdir"]).decode().strip()
dgtl = ctypes.CDLL(f"{lib_dir}/dgtl.dll" if False else None) # Windows C runtime lookup

class DgtlErr(ctypes.Structure):
    _fields_ = [("code", ctypes.c_int32), ("severity", ctypes.c_uint32)]

# Function bindings
dgtl_tm_create_host = dgtl.dgtl_target_machine_create_host
dgtl_tm_create_host.restype = ctypes.c_void_p

dgtl_tm_get_triple = dgtl.dgtl_target_machine_get_triple
dgtl_tm_get_triple.argtypes = [ctypes.c_void_p]
dgtl_tm_get_triple.restype = ctypes.c_char_p

dgtl_tm_destroy = dgtl.dgtl_target_machine_destroy
dgtl_tm_destroy.argtypes = [ctypes.c_void_p]

# Usage
tm = dgtl_tm_create_host()
print("Host Triple:", dgtl_tm_get_triple(tm).decode('utf-8'))
dgtl_tm_destroy(tm)
```

---

### C. Zig Integration
```zig
const std = @import("std");
const dgtl = @cImport({
    @cInclude("dgtl_sdk/dgtl.h");
    @cInclude("dgtl_sdk/target.h");
});

pub fn main() !void {
    const tm = dgtl.dgtl_target_machine_create_host();
    defer dgtl.dgtl_target_machine_destroy(tm);

    const triple = dgtl.dgtl_target_machine_get_triple(tm);
    std.debug.print("Host Target: {s}\n", .{triple});
}
```

---

## 13. Troubleshooting, Common Pitfalls & Best Practices

1. **Missing Multiple-Definition Flag during MinGW Linking:**
   - **Symptom:** Link errors mentioning duplicate symbols.
   - **Fix:** Pass `-Wl,--allow-multiple-definition` when linking with `g++` / MinGW-w64.
2. **Calling External Printf / C Runtime Functions from JIT:**
   - **Symptom:** Crash at address `0x00000000` or Access Violation.
   - **Fix:** Always register runtime functions with `dgtl_jit_engine_register_symbol` or provide a `dgtl_symbol_resolver_fn` callback returning the exact host address.
3. **Loop Register Allocation Spilling:**
   - **Tip:** When writing frontends that lower loops, do not hardcode temporary registers; allow DGTL's Universal Graph Coloring Register Allocator (`dgtl::regalloc`) to color and insert stack spills automatically.
4. **Immediate Constants vs Value Refs:**
   - In DGTL IR, 64-bit immediates are split across `op_a` (low 32 bits) and `op_b` (high 32 bits). When using `dgtl_func_builder_t`, always use `dgtl_build_const_i64` or `fb.k_i64()` rather than manually bit-shifting slots.
5. **Verifying Installation:**
   - Run `myzon dgtl-status` to verify that all 7 sovereign tool components report `[READY]`.
