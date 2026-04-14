# DGTL IR Types (IR_Types.hpp)

## Control Flow Map

```
[IR Types Module]
    |
    +--> Global Constants
    |       +--> INVALID_SLOT = 0xFFFFFFFF
    |       +--> NULL_REGISTER = 0x0
    |
    +--> OpCode Enumeration
    |       +--> NOP = 0
    |       +--> Memory Operations (10-19)
    |       |       +--> LOAD, STORE, REG_MOVE, LOAD_IMM, LEA
    |       +--> Integer Arithmetic I32 (20-29)
    |       |       +--> ADD_I32, SUB_I32, MUL_I32, DIV_I32, SHL_I32, AND_I32, OR_I32, XOR_I32
    |       +--> Floating Point F32 (30-39)
    |       |       +--> ADD_F32, SUB_F32, MUL_F32, DIV_F32, FMA_F32
    |       +--> SIMD/Vector Extensions (0x500+)
    |       |       +--> GATHER_F32_512, GATHER_16, STORE_V512, VEC_ADD_F32_512, VEC_MUL_F32_512
    |       +--> Control Flow (99-119)
    |       |       +--> GOTO, BRANCH, RET/RETURN, BRANCH_COND, BRANCH_COMPARE_FUSED, CALL, SYSCALL
    |       +--> Bit Manipulation (150-160)
    |       |       +--> BIT_FIELD_INSERT, BIT_FIELD_EXTRACT
    |       +--> Specialization Opcodes (200-210)
    |       |       +--> GUARD_VALUE, IMMEDIATE, PHI_NODE
    |       +--> Dialect Extensions (0x8000+)
    |               +--> CUSTOM_DIALECT_START, GUARD_ALIAS
    |
    +--> Flags Structure
    |       +--> Optimization Hints
    |       |       +--> IS_SPEC_CONST, IS_SPILL_CANDIDATE, REQUIRES_STACK_EMIT, HAS_STACK_OPERAND
    |       |       +--> IS_FUSED, IS_OPTIMIZED, IS_DEAD, IS_SIMD_BURST, REQUIRES_VEC_PORT
    |       +--> Side Effects & Control Flow
    |       |       +--> HAS_SIDE_EFFECTS, IS_RETURN, IS_VOLATILE, IS_CONST, IS_MACRO_FUSED
    |       |       +--> SVE_ACTIVE, BRANCH_LIKELY, BRANCH_UNLIKELY
    |       +--> Advanced Hints
    |               +--> REQUIRES_DEOPT_TARGET, IS_HOISTED
    |
    +--> ExtendedFlags Structure
    |       +--> Advanced Optimizations
    |               +--> IS_TILING_OPTIMIZED, IS_PREFETCHED
    |
    +--> IRAtom Structure (4 bytes)
    |       +--> OpCode opcode
    |       +--> uint16_t flags
    |
    +--> IRCore Structure (20 bytes)
    |       +--> uint32_t result_id
    |       +--> uint32_t op_a
    |       +--> uint32_t op_b
    |       +--> uint32_t op_c
    |       +--> uint32_t intent_hash
    |
    +--> Static Assertions
            +--> sizeof(IRAtom) == 4
            +--> sizeof(IRCore) == 20
```

**Flow Description**: The IR types define the fundamental data structures and enumerations for DGTL's Bit-Packed Intermediate Representation. It establishes the opcode space, flag system, and tiered atom/core structure for high-density IR storage.

## Summary

`IR_Types.hpp` defines the fundamental type system for DGTL's Bit-Packed Intermediate Representation (BIR), providing opcodes, flags, and data structures for high-density IR storage. It establishes the 24-byte node format with Tier 1 (IRAtom) and Tier 2 (IRCore) separation for cache-efficient processing.

Key features:
- 16-bit opcode enumeration with standard and extension ranges
- 16-bit flag system for optimization hints and side effects
- 4-byte IRAtom structure for opcode and flags
- 20-byte IRCore structure for operands and intent hash
- Static size assertions for HFT stride consistency
- Support for SIMD, control flow, and dialect extensions

## Full Explanation

### Architecture Overview
The IR_Types module establishes the foundation for DGTL's BIR (Bit-Packed Intermediate Representation):

1. **Opcode Space**: 16-bit enumeration with reserved ranges
2. **Flag System**: 16-bit hints for optimization and execution
3. **Tiered Storage**: Separate atom and core structures
4. **Size Constraints**: Static assertions for performance
5. **Extensibility**: Custom dialect support

### Global Constants

#### Sentinel Values
```cpp
static constexpr uint32_t INVALID_SLOT = 0xFFFFFFFF;
static constexpr uint32_t NULL_REGISTER = 0x0;
```

- **INVALID_SLOT**: Marks unused operand slots
- **NULL_REGISTER**: Represents unassigned registers
- **32-bit Values**: Match operand field sizes

### Opcode Enumeration

#### Core Opcodes
```cpp
enum class OpCode : uint16_t {
    NOP = 0,
    
    // Memory Operations
    LOAD = 10, STORE = 11, REG_MOVE = 12, LOAD_IMM = 13, LEA = 14,
    
    // Integer Arithmetic
    ADD_I32 = 20, SUB_I32 = 21, MUL_I32 = 22, DIV_I32 = 23,
    SHL_I32 = 24, AND_I32 = 25, OR_I32 = 26, XOR_I32 = 27,
    
    // Floating Point
    ADD_F32 = 30, SUB_F32 = 31, MUL_F32 = 32, DIV_F32 = 33, FMA_F32 = 35,
    
    // SIMD/Vector
    GATHER_F32_512 = 0x500, GATHER_16 = 0x501, STORE_V512 = 0x502,
    VEC_ADD_F32_512 = 0x503, VEC_MUL_F32_512 = 0x504,
    
    // Control Flow
    GOTO = 99, BRANCH = 100, RET = 101, RETURN = 101,
    BRANCH_COND = 102, BRANCH_COMPARE_FUSED = 103,
    CALL = 110, SYSCALL = 111,
    
    // Bit Manipulation
    BIT_FIELD_INSERT = 150, BIT_FIELD_EXTRACT = 151,
    
    // Specialization
    GUARD_VALUE = 200, IMMEDIATE = 201, PHI_NODE = 202,
    
    // Dialect Extensions
    CUSTOM_DIALECT_START = 0x8000, GUARD_ALIAS = 0x8001
};
```

- **NOP (0)**: No operation placeholder
- **Memory (10-19)**: Load/store and data movement
- **Integer (20-29)**: 32-bit arithmetic and logic
- **Float (30-39)**: 32-bit floating point operations
- **SIMD (0x500+)**: AVX-512 vector operations
- **Control Flow (99-119)**: Branching and function calls
- **Bit Ops (150-160)**: Bit field manipulation
- **Specialization (200-210)**: Profile-guided optimization
- **Extensions (0x8000+)**: Custom dialect space

### Flag System

#### Optimization and Execution Flags
```cpp
struct Flags {
    static constexpr uint16_t IS_SPEC_CONST = 0x0001;        // Specialized constant
    static constexpr uint16_t IS_SPILL_CANDIDATE = 0x0002;   // Register spill candidate
    static constexpr uint16_t REQUIRES_STACK_EMIT = 0x0004;  // Stack emission required
    static constexpr uint16_t HAS_STACK_OPERAND = 0x0008;    // Uses stack operand
    static constexpr uint16_t IS_FUSED = 0x0010;             // Macro fusion applied
    static constexpr uint16_t IS_OPTIMIZED = 0x0020;         // Optimization applied
    static constexpr uint16_t IS_DEAD = 0x0040;              // Dead code
    static constexpr uint16_t IS_SIMD_BURST = 0x0080;        // SIMD burst operation
    static constexpr uint16_t REQUIRES_VEC_PORT = 0x0100;    // Vector execution port
    
    static constexpr uint16_t HAS_SIDE_EFFECTS = 0x0200;     // Side effects present
    static constexpr uint16_t IS_RETURN = 0x0400;            // Return instruction
    static constexpr uint16_t IS_VOLATILE = 0x0800;          // Volatile access
    static constexpr uint16_t IS_CONST = 0x1000;             // Constant value
    static constexpr uint16_t IS_MACRO_FUSED = 0x2000;       // Macro fusion
    static constexpr uint16_t SVE_ACTIVE = 0x4000;           // SVE active
    static constexpr uint16_t BRANCH_LIKELY = 0x4000;        // Branch prediction hint
    static constexpr uint16_t BRANCH_UNLIKELY = 0x8000;      // Branch prediction hint
};
```

- **Optimization Hints**: IS_SPEC_CONST, IS_OPTIMIZED, IS_DEAD
- **Execution Requirements**: REQUIRES_VEC_PORT, IS_SIMD_BURST
- **Side Effects**: HAS_SIDE_EFFECTS, IS_VOLATILE
- **Control Flow**: IS_RETURN, branch prediction hints
- **Stack Operations**: Spill candidates and stack operands

#### Extended Flags
```cpp
struct ExtendedFlags {
    static constexpr uint32_t IS_TILING_OPTIMIZED = 0x00010000;  // Tiling applied
    static constexpr uint32_t IS_PREFETCHED = 0x00020000;        // Prefetching enabled
};
```

- **Advanced Optimizations**: Tiling and prefetching hints
- **32-bit Space**: Extended flag range beyond 16 bits

### Data Structures

#### IRAtom (Tier 1)
```cpp
#pragma pack(push, 1)
struct IRAtom {
    OpCode opcode;     // 2 bytes
    uint16_t flags;    // 2 bytes
}; // Total: 4 bytes
#pragma pack(pop)
```

- **Opcode Storage**: 16-bit operation code
- **Flag Storage**: 16-bit optimization hints
- **Cache Efficient**: 4-byte structure for dense packing

#### IRCore (Tier 2)
```cpp
#pragma pack(push, 1)
struct IRCore {
    uint32_t result_id;   // Result register/slot
    uint32_t op_a;        // First operand
    uint32_t op_b;        // Second operand
    uint32_t op_c;        // Third operand
    uint32_t intent_hash; // Sentinel verification hash
}; // Total: 20 bytes
#pragma pack(pop)
```

- **Result ID**: Destination register identifier
- **Operands**: Up to three source operands
- **Intent Hash**: Pillar 3 sentinel verification
- **20-byte Size**: Required for sentinel alignment

### Size Invariants

#### Static Assertions
```cpp
static_assert(sizeof(IRAtom) == 4, "IRAtom must be 4 bytes for cache-line packing");
static_assert(sizeof(IRCore) == 20, "IRCore must be 20 bytes to satisfy Sentinel intent_hash alignment");
```

- **HFT Consistency**: 4-byte atoms for stride optimization
- **Sentinel Alignment**: 20-byte cores for hash verification
- **Compile-Time Checks**: Prevent accidental size changes

### Integration Points

- **IR Stream**: Building block for IR construction
- **Code Generation**: Opcode mapping to machine instructions
- **Optimization Passes**: Flag-driven transformation decisions
- **Register Allocation**: Spill candidate identification
- **Sentinel System**: Intent hash verification

### Usage Example

```cpp
#include "dgtl/ir/IR_Types.hpp"

int main() {
    // Create an IR atom
    dgtl::ir::IRAtom atom;
    atom.opcode = dgtl::ir::OpCode::ADD_I32;
    atom.flags = dgtl::ir::Flags::IS_OPTIMIZED | dgtl::ir::Flags::IS_SPEC_CONST;
    
    // Create an IR core
    dgtl::ir::IRCore core;
    core.result_id = 5;        // Result goes to register 5
    core.op_a = 2;             // First operand from register 2
    core.op_b = 3;             // Second operand from register 3
    core.op_c = dgtl::ir::INVALID_SLOT;  // No third operand
    core.intent_hash = 0;      // To be computed by sentinel
    
    // Check sizes at compile time
    static_assert(sizeof(atom) == 4);
    static_assert(sizeof(core) == 20);
    
    // Use flags
    if (atom.flags & dgtl::ir::Flags::IS_SPEC_CONST) {
        // This is a specialized constant
    }
    
    return 0;
}
```

### Backward Compatibility

```cpp
namespace dgtl::ir {
    using namespace dgtl::ir;  // Legacy alias support
}
```

### Future Enhancements

- Extended opcode ranges for new ISAs
- Additional flag bits for emerging optimizations
- Variable operand counts
- Metadata attachments
- Type system integration

This header provides the type foundation for DGTL's intermediate representation, establishing the data structures and enumerations that enable high-performance, optimization-aware code generation.