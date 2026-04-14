# IR_Node.hpp Documentation

## Map of the Control Flow

```
IRNode Unified View Structure
├── Header (Tier 1 Mirror)
│   ├── OpCode opcode (2 bytes)
│   └── uint16_t flags (2 bytes)
├── Payload (Tier 2 Mirror)
│   ├── uint32_t result_id (4 bytes)
│   ├── uint32_t op_a (4 bytes)
│   ├── uint32_t op_b (4 bytes)
│   ├── uint32_t op_c (4 bytes)
│   └── uint32_t intent_hash (4 bytes)
├── Semantic Queries
│   ├── is_terminator() → bool
│   ├── is_volatile() → bool
│   ├── has_side_effects() → bool
│   └── is_valid() → bool
├── Validation & Hashing
│   ├── logic_hash() → uint64_t
│   ├── calculate_hash() → uint32_t (static)
│   └── refresh_intent_hash() → void
├── Optimization
│   ├── predict_inlined_benefit() → double
│   ├── clone() → IRNode
│   └── fold_if_possible() → void
└── Invariants
    └── static_assert(sizeof(IRNode) == 24)
```

**Data Flow:**
1. **Construction**: IRNode maps IRAtom (header) + IRCore (payload) into unified 24-byte structure
2. **Semantic Analysis**: Query methods check opcode/flags for control flow and side effects
3. **Validation**: logic_hash() combines intent_hash + result_id for collision detection
4. **Optimization**: predict_inlined_benefit() guides inlining, fold_if_possible() performs constant folding
5. **Hashing**: calculate_hash() uses Murmur-inspired mixing for low-collision intent hashing

## Summary

IR_Node.hpp defines the `IRNode` struct, which provides a unified 24-byte view of DGTL's bit-packed intermediate representation (BIR) instructions. This structure serves as the primary interface for high-level IR manipulation, combining Tier 1 (Atom) header information with Tier 2 (Core) payload data. The design enables zero-overhead mapping between different IR tiers while providing semantic analysis capabilities, validation hashing, and optimization heuristics.

Key features include:
- **Unified Structure**: 24-byte layout with opcode/flags header and operand/result payload
- **Semantic Queries**: Methods to determine terminators, volatility, side effects, and validity
- **Hash-based Validation**: Logic hashing for intent verification and collision detection
- **Optimization Support**: Inlining prediction and constant folding capabilities
- **HFT Alignment**: Strict size constraints for sub-millisecond execution tiers

The implementation follows DGTL's zero-overhead principle with noexcept operations and compile-time assertions ensuring memory layout consistency.

## Full Explanation of Code

### Namespace and Includes
```cpp
#pragma once
#include <cstdint>
#include <algorithm>
#include "IR_Types.hpp"

namespace dgtl::ir {
#pragma pack(push, 1)
```
- **#pragma once**: Standard header guard for single inclusion
- **Includes**: `<cstdint>` for fixed-width integers, `<algorithm>` for std::min/max (though not used here), `"IR_Types.hpp"` for OpCode and Flags definitions
- **Namespace**: Places all declarations in `dgtl::ir` namespace
- **#pragma pack(push, 1)**: Forces 1-byte struct alignment for exact 24-byte size requirement

### IRNode Struct Definition
```cpp
struct IRNode {
    // --- Header (Tier 1 Mirror) ---
    OpCode   opcode;    // 2 bytes
    uint16_t flags;     // 2 bytes

    // --- Payload (Tier 2 Mirror) ---
    uint32_t result_id; // 4 bytes
    uint32_t op_a;      // 4 bytes
    uint32_t op_b;      // 4 bytes
    uint32_t op_c;      // 4 bytes
    uint32_t intent_hash; // 4 bytes
```
The `IRNode` struct is the core unified representation:
- **Header Section**: Mirrors IRAtom structure with opcode (2 bytes) and flags (2 bytes)
- **Payload Section**: Mirrors IRCore structure with result_id and three operands (4 bytes each)
- **Intent Hash**: 4-byte hash for operation intent validation
- **Total Size**: Exactly 24 bytes (2+2+4+4+4+4+4) for HFT alignment

### Semantic Query Methods

#### is_terminator()
```cpp
[[nodiscard]] inline bool is_terminator() const noexcept {
    return opcode == OpCode::BRANCH || 
           opcode == OpCode::RET || 
           opcode == OpCode::RETURN || 
           opcode == OpCode::BRANCH_COND ||
           opcode == OpCode::GOTO;
}
```
- **Purpose**: Identifies control flow termination instructions
- **Return**: true if opcode indicates end of basic block
- **Opcodes**: BRANCH (unconditional), RET/RETURN (function return), BRANCH_COND (conditional), GOTO (jump)
- **Usage**: Critical for basic block analysis and control flow graphs

#### is_volatile()
```cpp
[[nodiscard]] inline bool is_volatile() const noexcept { 
    return (flags & Flags::IS_VOLATILE) != 0; 
}
```
- **Purpose**: Checks if operation has volatile memory semantics
- **Logic**: Tests IS_VOLATILE flag bit
- **Usage**: Prevents certain optimizations on volatile operations

#### has_side_effects()
```cpp
[[nodiscard]] inline bool has_side_effects() const noexcept { 
    return (flags & Flags::HAS_SIDE_EFFECTS) != 0 || 
           (flags & Flags::IS_RETURN) != 0 ||
           opcode == OpCode::STORE || 
           opcode == OpCode::STORE_V512 ||
           opcode == OpCode::CALL ||
           opcode == OpCode::SYSCALL ||
           opcode == OpCode::RET ||
           opcode == OpCode::RETURN; 
}
```
- **Purpose**: Determines if operation modifies external state
- **Logic**: Checks multiple conditions:
  - HAS_SIDE_EFFECTS flag set
  - IS_RETURN flag set (function returns)
  - Memory store operations (STORE, STORE_V512)
  - Function calls (CALL, SYSCALL)
  - Return instructions (RET, RETURN)
- **Usage**: Essential for optimization safety and dependency analysis

#### is_valid()
```cpp
[[nodiscard]] inline bool is_valid() const noexcept { 
    return opcode != OpCode::NOP; 
}
```
- **Purpose**: Checks if node represents a valid operation
- **Logic**: Any opcode except NOP is considered valid
- **Usage**: Filtering out no-operation placeholders

### Validation and Hashing

#### logic_hash()
```cpp
[[nodiscard]] inline uint64_t logic_hash() const noexcept {
    return (static_cast<uint64_t>(intent_hash) << 32) | result_id;
}
```
- **Purpose**: Creates 64-bit hash for Pillar 3 validation
- **Logic**: Combines intent_hash (high 32 bits) with result_id (low 32 bits)
- **Usage**: Strict collision checking for operation identity

#### calculate_hash() - Static Method
```cpp
static constexpr uint32_t calculate_hash(OpCode op, uint32_t a, uint32_t b, uint32_t c) noexcept {
    uint32_t h = static_cast<uint32_t>(static_cast<uint16_t>(op));
    auto mix = [&](uint32_t val) {
        h ^= val + 0x9e3779b9 + (h << 6) + (h >> 2);
    };
    mix(a);
    mix(b);
    mix(c);
    return h;
}
```
- **Purpose**: Computes low-collision hash for operation intent
- **Algorithm**: MurmurHash-inspired mixing function
- **Inputs**: opcode and three operands (a, b, c)
- **Constants**: 0x9e3779b9 (golden ratio), bit shifts for avalanche effect
- **Usage**: Generates intent_hash for validation

#### refresh_intent_hash()
```cpp
inline void refresh_intent_hash() noexcept {
    intent_hash = calculate_hash(opcode, op_a, op_b, op_c);
}
```
- **Purpose**: Updates intent hash after operand changes
- **Logic**: Recalculates hash using current opcode and operands
- **Usage**: Maintains validation integrity during transformations

### Optimization Methods

#### predict_inlined_benefit()
```cpp
[[nodiscard]] inline double predict_inlined_benefit() const noexcept {
    if (opcode == OpCode::GATHER_F32_512 || opcode == OpCode::GATHER_16) return 50.0;
    if (opcode == OpCode::SYSCALL) return 100.0; 
    return 1.0; 
}
```
- **Purpose**: Estimates performance benefit of inlining this operation
- **Logic**: 
  - GATHER operations: 50.0 (high benefit for vector gathers)
  - SYSCALL: 100.0 (maximum benefit for system calls)
  - Default: 1.0 (neutral benefit)
- **Usage**: Guides inlining decisions in optimization passes

#### clone()
```cpp
[[nodiscard]] inline IRNode clone() const noexcept { return *this; }
```
- **Purpose**: Creates identical copy of the node
- **Logic**: Simple copy constructor (bitwise copy)
- **Usage**: Node duplication during transformations

#### fold_if_possible()
```cpp
inline void fold_if_possible() noexcept {
    if (!(flags & Flags::IS_SPEC_CONST)) return;
    if (opcode == OpCode::ADD_I32 || opcode == OpCode::MUL_I32) {
        opcode = OpCode::IMMEDIATE;
        refresh_intent_hash();
    }
}
```
- **Purpose**: Performs zero-latency constant folding
- **Conditions**: 
  - Must have IS_SPEC_CONST flag set
  - Must be ADD_I32 or MUL_I32 operations
- **Action**: Converts to IMMEDIATE opcode and updates hash
- **Usage**: Constant propagation optimization

### Struct Closing and Assertions
```cpp
#pragma pack(pop)

// --- Invariants for HFT/Sub-millisecond execution tiers ---
// IRAtom and IRCore are defined in IR_Types.hpp to maintain SoA consistency.
static_assert(sizeof(IRNode) == 24, "IRNode unified view must be exactly 24 bytes for HFT alignment");
```
- **#pragma pack(pop)**: Restores default struct alignment
- **static_assert**: Compile-time verification of exact 24-byte size
- **Comment**: References IRAtom/IRCore definitions in IR_Types.hpp

### Backward Compatibility
```cpp
/**
 * @brief Backward Compatibility Alias for Legacy DGTL Logic.
 */
namespace dgtl::ir {
    using namespace dgtl::ir;
}
```
- **Purpose**: Maintains compatibility with legacy code
- **Logic**: Imports current namespace into itself (no-op but explicit)

## Usage Example
```cpp
#include "IR_Node.hpp"

// Create a node for addition
IRNode add_node;
add_node.opcode = OpCode::ADD_I32;
add_node.result_id = 1;
add_node.op_a = 10;  // operand A
add_node.op_b = 20;  // operand B
add_node.op_c = 0;   // unused
add_node.refresh_intent_hash();

// Check properties
if (add_node.has_side_effects()) { /* handle side effects */ }
if (add_node.is_terminator()) { /* handle control flow */ }

// Validate with logic hash
uint64_t validation_key = add_node.logic_hash();
```

## Dependencies
- **IR_Types.hpp**: Provides OpCode enum and Flags struct definitions
- **Standard Library**: `<cstdint>` for uint16_t/uint32_t/uint64_t types

## Thread Safety
All methods are `const noexcept` where applicable, making IRNode suitable for concurrent read access. Modification methods should be protected by external synchronization.

## Performance Characteristics
- **Size**: Fixed 24 bytes for cache efficiency
- **Alignment**: Packed for SIMD operations
- **Operations**: All queries are O(1) with minimal branching
- **Hashing**: Optimized for low latency with compile-time constants