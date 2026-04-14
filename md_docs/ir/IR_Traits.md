# DGTL IR Traits (IR_Traits.hpp)

## Control Flow Map

```
[IRTraits Structure]
    |
    +--> Control Flow Analysis
    |       +--> is_branch(OpCode op)
    |       |       +--> Check for BRANCH, BRANCH_COND, BRANCH_COMPARE_FUSED, GOTO
    |       |       +--> Return true if branch operation
    |       |
    |       +--> is_terminator(OpCode op)
    |               +--> Check for branches or RET/RETURN
    |               +--> Return true if ends basic block
    |
    +--> Memory Operation Detection
    |       +--> is_memory_op(OpCode op)
    |               +--> Check for LOAD, STORE, GATHER, LEA operations
    |               +--> Return true if accesses memory
    |
    +--> Side Effect Analysis
    |       +--> has_side_effects(OpCode op, uint16_t flags)
    |       |       +--> Check flag bits (HAS_SIDE_EFFECTS, IS_VOLATILE, IS_RETURN)
    |       |       +--> Switch on opcode for STORE, CALL, SYSCALL, branches, guards
    |       |       +--> Return true if has side effects
    |
    +--> SIMD Detection
    |       +--> is_simd(OpCode op)
    |               +--> Check for AVX-512 operations (GATHER, STORE_V512, VEC_*)
    |               +--> Return true if SIMD operation
    |
    +--> Validation Functions
    |       +--> is_valid_slot(uint32_t slot_id)
    |               +--> Check not INVALID_SLOT and within 24-bit range
    |               +--> Return true if valid slot ID
    |
    +--> Optimization Heuristics
    |       +--> get_inline_weight(OpCode op)
    |               +--> Return weight for inlining decisions
    |               +--> High weight for gathers (50.0), syscalls (100.0)
    |               +--> Low weight for branches (5.0), default (1.0)
    |
    +--> Static Assertions
            +--> sizeof(IRAtom) == 4
            +--> sizeof(IRCore) == 20
```

**Flow Description**: The IR traits provide compile-time semantic analysis functions for opcodes and flags, enabling zero-overhead classification of operations for optimization passes, control flow analysis, and safety verification.

## Summary

`IR_Traits.hpp` implements static utility functions for semantic analysis of DGTL's IR operations, providing zero-overhead classification and validation of opcodes, flags, and slot IDs. It serves as the traits engine for rapid semantic queries during compilation, optimization, and code generation phases.

Key features:
- Control flow analysis (branches, terminators)
- Memory operation detection
- Side effect classification for GVN safety
- SIMD operation identification
- Slot ID validation
- Inlining weight heuristics
- Zero-overhead constexpr functions

## Full Explanation

### Architecture Overview
The IRTraits structure provides compile-time semantic analysis for DGTL's IR:

1. **Control Flow**: Branch and terminator detection
2. **Memory Access**: Load/store operation identification
3. **Side Effects**: Safety analysis for optimizations
4. **SIMD Classification**: Vector operation detection
5. **Validation**: Slot ID and operand checking
6. **Optimization**: Heuristics for transformation decisions

### Control Flow Analysis

#### Branch Detection
```cpp
static constexpr bool is_branch(OpCode op) noexcept {
    return op == OpCode::BRANCH || 
           op == OpCode::BRANCH_COND || 
           op == OpCode::BRANCH_COMPARE_FUSED || 
           op == OpCode::GOTO;
}
```

- **Branch Operations**: Unconditional and conditional branches
- **Fused Operations**: Compare-and-branch combinations
- **GOTO**: Unconditional jump
- **Basic Block**: Used for CFG construction

#### Terminator Detection
```cpp
static constexpr bool is_terminator(OpCode op) noexcept {
    return is_branch(op) || op == OpCode::RET || op == OpCode::RETURN;
}
```

- **Block End**: Operations that terminate basic blocks
- **Return Operations**: Function exit points
- **CFG Construction**: Identifies block boundaries

### Memory Operation Classification

#### Memory Access Detection
```cpp
static constexpr bool is_memory_op(OpCode op) noexcept {
    return op == OpCode::LOAD || 
           op == OpCode::STORE || 
           op == OpCode::GATHER_F32_512 || 
           op == OpCode::GATHER_16 ||
           op == OpCode::STORE_V512 ||
           op == OpCode::LEA;
}
```

- **Load/Store**: Basic memory operations
- **Gather/Scatter**: SIMD memory operations
- **LEA**: Address calculation
- **Dependency Analysis**: Memory access identification

### Side Effect Analysis

#### Comprehensive Side Effect Detection
```cpp
static constexpr bool has_side_effects(OpCode op, uint16_t flags) noexcept {
    // Flag-based detection
    if (flags & Flags::HAS_SIDE_EFFECTS) return true; 
    if (flags & Flags::IS_VOLATILE) return true; 
    if (flags & Flags::IS_RETURN) return true;
    
    // Opcode-based detection
    switch (op) {
        case OpCode::STORE:
        case OpCode::STORE_V512:
        case OpCode::CALL:
        case OpCode::SYSCALL:
        case OpCode::RET:
        case OpCode::BRANCH:
        case OpCode::BRANCH_COND:
        case OpCode::GOTO:
        case OpCode::GUARD_ALIAS:
        case OpCode::GUARD_VALUE:
            return true;
        default:
            return false;
    }
}
```

- **Flag Priority**: Explicit flag checks first
- **Opcode Safety**: Conservative classification
- **GVN Safety**: Prevents unsafe deduplication
- **Pillar 3**: Sentinel side effect detection

### SIMD Operation Detection

#### Vector Operation Classification
```cpp
static constexpr bool is_simd(OpCode op) noexcept {
    return op == OpCode::GATHER_F32_512 || 
           op == OpCode::GATHER_16 ||
           op == OpCode::STORE_V512 ||
           op == OpCode::VEC_ADD_F32_512 ||
           op == OpCode::VEC_MUL_F32_512;
}
```

- **AVX-512 Operations**: 512-bit vector instructions
- **Gather Operations**: Indexed vector loads
- **Vector Arithmetic**: SIMD add/multiply
- **Optimization**: SIMD-aware code generation

### Validation Functions

#### Slot ID Validation
```cpp
static constexpr bool is_valid_slot(uint32_t slot_id) noexcept {
    return slot_id != INVALID_SLOT && slot_id < 0xFFFFFF00;
}
```

- **Invalid Check**: Reject sentinel INVALID_SLOT
- **Range Check**: 24-bit addressable range
- **Pillar 3**: Invariant validation
- **Safety**: Prevent invalid slot access

### Optimization Heuristics

#### Inlining Weight Calculation
```cpp
static constexpr double get_inline_weight(OpCode op) noexcept {
    if (op == OpCode::GATHER_F32_512 || op == OpCode::GATHER_16) return 50.0;
    if (is_branch(op)) return 5.0;
    if (op == OpCode::SYSCALL) return 100.0; 
    return 1.0;
}
```

- **Gather Penalty**: High cost for indexed loads (50.0)
- **Branch Cost**: Moderate penalty for control flow (5.0)
- **Syscall Cost**: Very high cost for system calls (100.0)
- **Default**: Base weight for other operations (1.0)

### Static Assertions

#### Size Invariants
```cpp
static_assert(sizeof(IRAtom) == 4, "IRAtom must be 4 bytes for cache-line packing");
static_assert(sizeof(IRCore) == 20, "IRCore must be 20 bytes to satisfy Sentinel intent_hash alignment");
```

- **Atom Size**: 4-byte requirement for cache efficiency
- **Core Size**: 20-byte requirement for sentinel alignment
- **HFT Optimization**: Stride consistency for high-frequency trading

### Integration Points

- **CFG Construction**: Branch and terminator detection
- **GVN Pass**: Side effect analysis for safety
- **Memory Analysis**: Memory operation classification
- **Register Allocation**: SIMD operation awareness
- **Inlining**: Weight-based decision making
- **Validation**: Slot ID checking

### Usage Example

```cpp
#include "dgtl/ir/IR_Traits.hpp"

int main() {
    using namespace dgtl::ir;
    
    // Control flow analysis
    OpCode op = OpCode::BRANCH_COND;
    bool is_branch_op = IRTraits::is_branch(op);           // true
    bool is_terminator_op = IRTraits::is_terminator(op);   // true
    
    // Memory operation detection
    OpCode mem_op = OpCode::LOAD;
    bool is_memory = IRTraits::is_memory_op(mem_op);       // true
    
    // Side effect analysis
    uint16_t flags = Flags::HAS_SIDE_EFFECTS;
    OpCode store_op = OpCode::STORE;
    bool has_effects = IRTraits::has_side_effects(store_op, flags);  // true
    
    // SIMD detection
    OpCode vec_op = OpCode::VEC_ADD_F32_512;
    bool is_simd_op = IRTraits::is_simd(vec_op);            // true
    
    // Slot validation
    uint32_t valid_slot = 42;
    uint32_t invalid_slot = INVALID_SLOT;
    bool valid = IRTraits::is_valid_slot(valid_slot);       // true
    bool invalid = IRTraits::is_valid_slot(invalid_slot);   // false
    
    // Inlining weights
    double gather_weight = IRTraits::get_inline_weight(OpCode::GATHER_16);     // 50.0
    double branch_weight = IRTraits::get_inline_weight(OpCode::BRANCH);        // 5.0
    double default_weight = IRTraits::get_inline_weight(OpCode::ADD_I32);      // 1.0
    
    return 0;
}
```

### Performance Characteristics

- **Zero Overhead**: All functions are constexpr
- **Compile-Time**: Classification decisions made at compile time
- **No Runtime Cost**: Static analysis only
- **Cache Friendly**: No memory access required
- **Branchless**: Simple comparisons and switches

### Backward Compatibility

```cpp
namespace dgtl::ir {
    using namespace dgtl::ir;  // Legacy support
}
```

### Future Enhancements

- Extended opcode classification
- Architecture-specific traits
- Dynamic trait registration
- Cost model integration
- Profile-guided trait adjustments

This header provides the semantic analysis foundation for DGTL's IR processing, enabling efficient classification and optimization decisions throughout the compilation pipeline.