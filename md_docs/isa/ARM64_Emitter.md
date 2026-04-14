# ARM64_Emitter.hpp Documentation

## Map of the Control Flow

```
ARM64 Assembly Emitter
├── Configuration (Config struct)
│   ├── use_neon: Enable NEON SIMD instructions
│   ├── position_indep: Position-independent code generation
│   ├── omit_fp: Skip frame pointer setup/teardown
│   └── verbose_asm: Include transaction hash comments
├── ARM64Emitter Class (64-byte aligned)
│   ├── Construction
│   │   ├── IRStream& m_stream: Source IR to emit
│   │   └── SymbolTable& m_symbols: Symbol resolution
│   ├── Main Emission
│   │   └── emit_assembly(out, config)
│   │       ├── emit_header(): Function prologue
│   │       ├── Instruction Loop
│   │       │   ├── Skip NOP instructions
│   │       │   ├── Detect SIMD operations
│   │       │   ├── Switch on OpCode: dispatch to emit_* methods
│   │       │   └── Add verbose comments with intent_hash
│   │       └── emit_footer(): Function epilogue
│   ├── Scalar Arithmetic Emission
│   │   └── emit_arithmetic(): ADD/SUB/MUL for int/float
│   │       ├── Integer: "add/sub/mul xDst, xSrcA, xSrcB"
│   │       └── Float: "fadd/fsub sDst, sSrcA, sSrcB"
│   ├── Memory Operations
│   │   ├── emit_load(): "ldr xDst, [xBase, #offset]"
│   │   └── emit_store(): "str xSrc, [xBase, #offset]"
│   ├── Vector Operations
│   │   └── emit_neon_vec_op(): NEON SIMD instructions
│   │       └── "fadd vDst.4s, vSrcA.4s, vSrcB.4s"
│   └── Control Flow
│       └── emit_ret(): Return with optional value
│           └── "mov x0, xVal" + "ret"
└── Namespace Aliases
    └── ir::ARM64Emitter = isa::ARM64Emitter
```

**Data Flow:**
1. **Configuration**: Set emission options (NEON, frame pointer, verbosity)
2. **Header Emission**: Output function prologue with architecture directives
3. **Instruction Processing**: For each IR node:
   - Skip NOP operations
   - Check SIMD flags for vector operations
   - Dispatch to appropriate emit_* method based on OpCode
   - Add transaction hash comments if verbose mode
4. **Footer Emission**: Output function epilogue and return
5. **Symbol Resolution**: Use SymbolTable for address resolution (future extension)

## Summary

ARM64_Emitter.hpp implements a backend assembly emitter for DGTL's compiler infrastructure, converting bit-packed intermediate representation (BIR) instructions into AArch64 assembly code. The emitter supports both scalar and NEON SIMD operations with configurable code generation options. It integrates with the transactional IR system by including intent hash validation in verbose output.

Key features include:
- **Multi-Architecture Support**: AArch64 assembly generation with ARMv8-A baseline
- **SIMD Integration**: NEON vector instruction emission for 512-bit operations
- **Configurable Emission**: Runtime options for frame pointers, SIMD usage, and verbosity
- **Transactional Validation**: Intent hash comments for debugging and verification
- **Memory Operations**: Load/store instruction generation with offset addressing
- **Type-Aware Emission**: Automatic register prefix selection (x for int, s for float)

The implementation follows DGTL's zero-overhead principle with direct IR-to-assembly mapping and minimal runtime overhead.

## Full Explanation of Code

### Namespace and Includes
```cpp
#pragma once
#include <cstdint>
#include <ostream>
#include <iomanip>
#include <string>

#include "../ir/Global_IR.hpp"
#include "../core/Target.hpp"
#include "../core/Symbol_Table.hpp"

namespace dgtl::isa {
```
- **#pragma once**: Header guard
- **Standard Includes**:
  - `<cstdint>`: Fixed-width integers
  - `<ostream>`: Output stream operations
  - `<iomanip>`: Stream manipulators (setw, hex, etc.)
  - `<string>`: String operations
- **Internal Includes**:
  - `"../ir/Global_IR.hpp"`: IR type definitions
  - `"../core/Target.hpp"`: Target architecture information
  - `"../core/Symbol_Table.hpp"`: Symbol resolution
- **Namespace**: dgtl::isa (instruction set architecture)

### Config Struct
```cpp
struct Config {
    bool use_neon       = true;
    bool position_indep  = true; 
    bool omit_fp        = true; 
    bool verbose_asm    = true;
    
    // Explicit constructor prevents MinGW "required before end of class" error
    constexpr Config() noexcept {} 
};
```
**Configuration Options**:
- **use_neon**: Enable NEON SIMD instruction emission
- **position_indep**: Generate position-independent code (future use)
- **omit_fp**: Skip frame pointer setup/teardown for leaf functions
- **verbose_asm**: Include transaction hash comments in output

**Constructor**: Explicit constexpr to avoid MinGW compilation issues

### ARM64Emitter Class
```cpp
class alignas(64) ARM64Emitter {
public:
    // ...
private:
    const IRStream& m_stream;
    const SymbolTable& m_symbols;
```
**Alignment**: 64-byte alignment prevents false sharing
**Members**:
- **m_stream**: Const reference to IR being emitted
- **m_symbols**: Const reference to symbol table for resolution

### Constructor
```cpp
explicit ARM64Emitter(const IRStream& stream, const SymbolTable& symbols)
    : m_stream(stream), m_symbols(symbols) {}
```
**Purpose**: Initialize emitter with IR source and symbol context
**References**: Store const references for efficient access

### Main Emission Method

#### emit_assembly()
```cpp
void emit_assembly(std::ostream& out, const Config config = {}) {
    if (m_stream.is_null() || m_stream.size() == 0) return;

    emit_header(out, config);

    for (size_t i = 0; i < m_stream.size(); ++i) {
        const auto& atom = m_stream.get_atom_readonly(i);
        const auto& core = m_stream.get_core_readonly(i);
        
        if (atom.opcode == OpCode::NOP) continue;
        out << "    "; 
        const bool is_simd = (atom.flags & Flags::IS_SIMD_BURST);

        switch (atom.opcode) {
            case OpCode::ADD_I32:   emit_arithmetic(out, "add", core); break;
            case OpCode::SUB_I32:   emit_arithmetic(out, "sub", core); break;
            case OpCode::MUL_I32:   emit_arithmetic(out, "mul", core); break;
            case OpCode::ADD_F32:   emit_arithmetic(out, "fadd", core, true); break;
            case OpCode::SUB_F32:   emit_arithmetic(out, "fsub", core, true); break;
            case OpCode::LOAD:      emit_load(out, core); break;
            case OpCode::STORE:     emit_store(out, core); break;
            case OpCode::RET:       emit_ret(out, core); break;
            case OpCode::VEC_ADD_F32_512: 
                if (config.use_neon || is_simd) emit_neon_vec_op(out, "fadd", core);
                break;
            default:
                out << "// [DGTL-EMIT-ERR] Unhandled: " << static_cast<uint32_t>(atom.opcode);
                break;
        }

        if (config.verbose_asm) {
            out << std::setw(28) << std::right << " // TR_HASH:0x" 
                << std::hex << std::uppercase << core.intent_hash << std::dec << std::nouppercase;
        }
        out << "\n";
    }
    emit_footer(out, config);
}
```
**Algorithm**: Linear pass over IR stream, emitting assembly for each instruction
**Processing**:
1. **Early Return**: Skip empty streams
2. **Header**: Emit function prologue
3. **Instruction Loop**:
   - Get atom (opcode/flags) and core (operands) for current node
   - Skip NOP instructions
   - Detect SIMD operations via IS_SIMD_BURST flag
   - Switch on OpCode to dispatch to specific emit_* method
   - Add transaction hash comment if verbose mode enabled
4. **Footer**: Emit function epilogue

### Header/Footer Emission

#### emit_header()
```cpp
void emit_header(std::ostream& out, const Config& config) {
    out << "// --- DGTL-MLIR-X ARM64 BACKEND ---\n";
    out << ".arch armv8-a\n.text\n.align 4\n.global _DGTL_entry\n\n_DGTL_entry:\n";
    if (!config.omit_fp) out << "    stp x29, x30, [sp, #-16]!\n    mov x29, sp\n";
}
```
**Directives**:
- **.arch armv8-a**: Specify ARMv8-A architecture
- **.text**: Code section
- **.align 4**: 16-byte alignment
- **.global _DGTL_entry**: Export entry point

**Frame Setup** (if not omitted):
- **stp x29, x30, [sp, #-16]!**: Push FP and LR, pre-decrement SP
- **mov x29, sp**: Set frame pointer

#### emit_footer()
```cpp
void emit_footer(std::ostream& out, const Config& config) {
    if (!config.omit_fp) out << "    ldp x29, x30, [sp], #16\n";
    out << "    ret\n";
}
```
**Frame Teardown** (if not omitted):
- **ldp x29, x30, [sp], #16**: Pop FP and LR, post-increment SP

**Return**: Unconditional return to caller

### Arithmetic Emission

#### emit_arithmetic()
```cpp
void emit_arithmetic(std::ostream& out, const char* instr, const IRCore& core, bool is_fp = false) {
    char prefix = is_fp ? 's' : 'x'; 
    out << instr << " " << prefix << core.op_c << ", " << prefix << core.op_a << ", " << prefix << core.op_b;
}
```
**Format**: "instr prefixDst, prefixSrcA, prefixSrcB"
**Integer**: "add x0, x1, x2" (64-bit registers)
**Float**: "fadd s0, s1, s2" (32-bit float registers)

### Memory Operations

#### emit_load()
```cpp
void emit_load(std::ostream& out, const IRCore& core) {
    out << "ldr x" << core.op_c << ", [x" << core.op_a << ", #" << core.op_b << "]";
}
```
**Format**: "ldr xDst, [xBase, #offset]"
**Semantics**: Load 64-bit value from memory at Base + offset into Dst

#### emit_store()
```cpp
void emit_store(std::ostream& out, const IRCore& core) {
    out << "str x" << core.op_a << ", [x" << core.op_b << ", #" << core.op_c << "]";
}
```
**Format**: "str xSrc, [xBase, #offset]"
**Semantics**: Store 64-bit value from Src to memory at Base + offset

### Vector Operations

#### emit_neon_vec_op()
```cpp
void emit_neon_vec_op(std::ostream& out, const char* instr, const IRCore& core) {
    out << instr << " v" << core.op_c << ".4s, v" << core.op_a << ".4s, v" << core.op_b << ".4s";
}
```
**Format**: "instr vDst.4s, vSrcA.4s, vSrcB.4s"
**Semantics**: NEON SIMD operation on 4x 32-bit float vectors
**Arrangement**: .4s = 4 single-precision floats (128-bit vector)

### Control Flow

#### emit_ret()
```cpp
void emit_ret(std::ostream& out, const IRCore& core) {
    if (core.op_a != 0xFF && core.op_a != 0) {
        out << "mov x0, x" << core.op_a << "\n    ";
    }
}
```
**Logic**: If return value specified (not 0xFF or 0), move to x0
**Convention**: ARM64 returns values in x0 register

### Namespace Aliases
```cpp
// This alias is CRITICAL. main.cpp uses ir::ARM64Emitter.
namespace dgtl::ir {
    using ARM64Emitter = dgtl::isa::ARM64Emitter;
}

namespace dgtl::ir {
    using ARM64Emitter = dgtl::isa::ARM64Emitter;
}
```
**Purpose**: Provide ir::ARM64Emitter alias for backward compatibility
**Usage**: Allows main.cpp and other code to use ir:: namespace

## Usage Example
```cpp
#include "ARM64_Emitter.hpp"

// Configure emission options
dgtl::isa::ARM64Emitter::Config config;
config.use_neon = true;
config.verbose_asm = true;
config.omit_fp = false;  // Include frame pointer for debugging

// Create emitter
dgtl::ir::IRStream stream(/*...*/);
dgtl::ir::SymbolTable symbols(/*...*/);
dgtl::ir::ARM64Emitter emitter(stream, symbols);

// Emit assembly to stdout
emitter.emit_assembly(std::cout, config);

// Output includes:
// - ARM64 assembly instructions
// - Transaction hash comments for validation
// - Proper function prologue/epilogue
```

## Dependencies
- **Global_IR.hpp**: OpCode, Flags, IRCore, IRAtom definitions
- **Target.hpp**: Target architecture information
- **Symbol_Table.hpp**: Symbol resolution interface
- **IRStream**: Source IR access methods

## Thread Safety
- **Emission**: Not thread-safe, single emitter per output stream
- **Read-Only Access**: Uses const references to IRStream and SymbolTable
- **Output Stream**: External synchronization required for shared streams

## Performance Characteristics
- **Time Complexity**: O(N) where N is IR instruction count
- **Space Complexity**: O(1) additional space beyond input IR
- **Memory Access**: Read-only access to IR buffers
- **Output Efficiency**: Direct stream writing with minimal formatting
- **SIMD Detection**: Runtime flag checking for vector operations
- **Code Size**: Compact emission with optional verbose comments