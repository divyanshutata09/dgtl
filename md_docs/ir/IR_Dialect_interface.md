# IR_Dialect_interface.hpp Documentation

## Map of the Control Flow

```
DGTL Dialect Interface & Verification System
├── Concept Definition
│   └── DGTLOp Concept
│       ├── Requires: opcode convertible to ir::OpCode
│       └── Requires: verify_impl(atom, core) → bool
├── Base Operation CRTP
│   └── BaseOp<Derived, OpCode>
│       ├── static constexpr opcode = Op
│       └── verify(atom, core) → dispatch to Derived::verify_impl
├── Dialect Implementation
│   └── MathDialect
│       ├── name = "DGTL.math"
│       ├── FastAdd : BaseOp<0x8001>
│       │   ├── verify_impl: result_id != 0 && op_a != 0
│       │   └── Intent hash validation (future)
│       └── FastFMA : BaseOp<0x8002>
│           └── verify_impl: all operands non-zero
├── Operation Registry
│   └── OpRegistry<Ops...>
│       ├── verify_stream(stream) → scan and validate
│       └── dispatch(atom, core) → fold expression verification
└── Verification Pipeline
    ├── Stream Scan: iterate through IRStream
    ├── Per-Node Dispatch: call Ops::verify for each node
    ├── Sentinel Check: verify_impl validates operation semantics
    ├── Intent Validation: check operation intent hash
    └── Result: bool (commit/rollback decision)
```

**Data Flow:**
1. **Concept Check**: Compile-time verification that operations meet DGTLOp requirements
2. **Operation Definition**: Dialects define operations inheriting from BaseOp
3. **Registry Creation**: OpRegistry instantiated with variadic operation types
4. **Stream Verification**: verify_stream() iterates through IRStream nodes
5. **Dispatch**: Fold expression calls verify() on each operation type
6. **Validation**: Each operation's verify_impl() checks semantic correctness
7. **Decision**: All operations must pass for commit, any failure triggers rollback

## Summary

IR_Dialect_interface.hpp implements DGTL's zero-cost static dialect interface using C++20 concepts and CRTP (Curiously Recurring Template Pattern). This system provides Pillar 3's Sentinel integration for shadow-validation, enabling compile-time verification of operation semantics and runtime intent validation. The design allows custom hardware dialects to be defined with invariant guards that prevent invalid IR mutations.

Key features include:
- **Concept-Based Design**: DGTLOp concept ensures operations meet interface requirements
- **CRTP BaseOp**: Provides static opcode and verification dispatch
- **MathDialect Example**: Hardware-accelerated vector math with anti-hallucination checks
- **OpRegistry Template**: Variadic template orchestrates multi-operation verification
- **Sentinel Verification**: Runtime validation prevents invalid commits
- **Zero-Overhead Dispatch**: Compile-time resolved verification calls

The implementation follows DGTL's zero-overhead principle with compile-time concept checking and efficient fold expressions for runtime validation.

## Full Explanation of Code

### Namespace and Includes
```cpp
#pragma once
#include <concepts>
#include <string_view>
#include <type_traits>
#include <array>

// Internal logic only needs the basic types. 
// Do NOT include IR_Entry_Point.hpp here to avoid circular dependency.
#include "IR_Types.hpp"

namespace dgtl::ir {
    // Forward declarations: Tells the compiler these exist without loading full guts.
    // This allows Dialect_interface to be Part 3 of the Entry Point safely.
    class IRStream;
    struct IRAtom;
    struct IRCore;
}

namespace dgtl::dialect {
```
- **#pragma once**: Header guard
- **Concepts Include**: C++20 concepts for compile-time verification
- **String View**: For dialect naming
- **Type Traits**: Template metaprogramming utilities
- **Array**: Fixed-size containers (not used in this file)
- **IR_Types.hpp**: Basic IR type definitions only (avoids circular dependencies)
- **Forward Declarations**: IRStream, IRAtom, IRCore to break circular dependencies
- **Dialect Namespace**: dgtl::dialect for dialect-related code

### DGTLOp Concept
```cpp
/**
 * @concept DGTLOp
 * @brief Operation Concept for zero-cost static verification.
 * Validates against the Tiered IR structure (Atom + Core).
 */
template <typename T>
concept DGTLOp = requires {
    { T::opcode } -> std::convertible_to<ir::OpCode>;
} && requires(const ir::IRAtom& atom, const ir::IRCore& core) {
    { T::verify_impl(atom, core) } -> std::same_as<bool>;
};
```
**Purpose**: Compile-time concept checking for dialect operations
**Requirements**:
- **opcode**: Static member convertible to ir::OpCode
- **verify_impl**: Static method taking const IRAtom& and const IRCore&, returning bool
**Usage**: Ensures all operations in a dialect meet the interface contract

### BaseOp CRTP Template
```cpp
/**
 * @struct BaseOp
 * @brief CRTP Base for all DGTL Operations.
 * Returns bool to signal the Sentinel (Pillar 3) whether to Rollback or Commit.
 */
template <typename Derived, ir::OpCode Op>
struct BaseOp {
    static constexpr ir::OpCode opcode = Op;
    
    static inline bool verify(const ir::IRAtom& atom, const ir::IRCore& core) noexcept {
        if (atom.opcode == opcode) [[likely]] {
            return Derived::verify_impl(atom, core);
        }
        return true; // Not our opcode, pass through.
    }
};
```
**CRTP Pattern**: Derived class provides verify_impl, BaseOp provides interface
**Static Opcode**: Compile-time constant for each operation
**Verify Logic**:
- Check if atom.opcode matches this operation's opcode
- If match: call Derived::verify_impl
- If no match: return true (pass through for other operations)
- [[likely]] hint for performance (most nodes won't match specific opcode)

### MathDialect Implementation
```cpp
/**
 * @struct MathDialect
 * @brief Hardware-Accelerated Vector Math Dialect.
 * Includes Anti-Hallucination checks for high-integrity compute.
 */
struct MathDialect {
    static constexpr std::string_view name = "DGTL.math";
```
**Purpose**: Example dialect for mathematical operations
**Name**: String identifier for the dialect

#### FastAdd Operation
```cpp
// FastAdd: Optimized 32-bit addition.
struct FastAdd : BaseOp<FastAdd, static_cast<ir::OpCode>(0x8001)> {
    static inline bool verify_impl([[maybe_unused]] const ir::IRAtom& atom, const ir::IRCore& core) noexcept {
        // PILLAR 3: Sentinel Check
        // FastAdd must produce a result and have at least one valid operand.
        if (core.result_id == 0 || core.op_a == 0) [[unlikely]] return false;
        
        // Intent Verification: Ensures IR mutation matches architectural intent.
        if (core.intent_hash != 0) {
            // Future: CRC check logic goes here.
        }
        return true;
    }
};
```
**Inheritance**: BaseOp with opcode 0x8001 (extended range for custom operations)
**Verification Logic**:
- **Sentinel Check**: result_id and op_a must be non-zero
- **[[unlikely]]**: Branch hint for error paths
- **Intent Hash**: Placeholder for future CRC validation
- **Return**: true if valid, false triggers rollback

#### FastFMA Operation
```cpp
// FastFMA: Fused Multiply-Add (Essential for HFT/ML workloads).
struct FastFMA : BaseOp<FastFMA, static_cast<ir::OpCode>(0x8002)> {
    static inline bool verify_impl([[maybe_unused]] const ir::IRAtom& atom, const ir::IRCore& core) noexcept {
        // FMA in DGTL requires all registers to be non-zero (initialized).
        return (core.result_id != 0 && core.op_a != 0 && core.op_b != 0);
    }
};
```
**Inheritance**: BaseOp with opcode 0x8002
**Verification Logic**: All operands (result_id, op_a, op_b) must be non-zero
**Purpose**: Prevents uninitialized register usage in FMA operations

### OpRegistry Template
```cpp
/**
 * @class OpRegistry
 * @brief Orchestrates the "DGTL-Invariant" Guard across the IRStream.
 */
template <DGTLOp... Ops>
class OpRegistry {
public:
    /**
     * @brief High-Throughput Sentinel Verification.
     * Scans the active stream. Since implementation requires IRStream methods, 
     * this must be defined where the full IRStream class is visible.
     */
    static bool verify_stream(ir::IRStream& stream) noexcept;
```
**Variadic Template**: Accepts any number of DGTLOp-conforming operations
**verify_stream**: Scans entire IRStream for validation (implementation in .cpp file)
**Purpose**: Orchestrates verification across multiple operation types

#### Private Dispatch Method
```cpp
private:
    /**
     * @brief O(1) Dispatch Logic via C++17 Fold Expression.
     */
    static inline bool dispatch(const ir::IRAtom& atom, const ir::IRCore& core) noexcept {
        return (... && Ops::verify(atom, core));
    }
```
**Fold Expression**: (... && Ops::verify(atom, core))
- **Expansion**: Op1::verify() && Op2::verify() && Op3::verify() && ...
- **Logic**: All operations must verify successfully (short-circuit on false)
- **Performance**: O(1) per node, compile-time generated dispatch

### Default Registry
```cpp
/**
 * @brief Default Registry for Standard Math Dialect.
 */
using StandardMathRegistry = OpRegistry<MathDialect::FastAdd, MathDialect::FastFMA>;
```
**Type Alias**: Pre-configured registry with standard math operations
**Usage**: Ready-to-use registry for basic math dialect validation

### Namespace Closing
```cpp
} // namespace dgtl::dialect
```
- Closes the dialect namespace

## Usage Example
```cpp
#include "IR_Dialect_interface.hpp"

// Define custom operation
struct CustomOp : dgtl::dialect::BaseOp<CustomOp, static_cast<dgtl::ir::OpCode>(0x9001)> {
    static bool verify_impl(const dgtl::ir::IRAtom& atom, const dgtl::ir::IRCore& core) noexcept {
        // Custom verification logic
        return core.result_id != 0 && core.op_a != core.op_b; // Example: operands must differ
    }
};

// Create registry with custom operations
using MyRegistry = dgtl::dialect::OpRegistry<dgtl::dialect::MathDialect::FastAdd, CustomOp>;

// Use in verification
dgtl::ir::IRStream stream(/*...*/);
{
    dgtl::ir::IRTransactionGuard guard(stream);
    // ... emit operations ...
    
    if (MyRegistry::verify_stream(stream)) {
        guard.commit();  // All operations valid
    }
    // Automatic rollback if verification fails
}
```

## Dependencies
- **IR_Types.hpp**: OpCode, IRAtom, IRCore definitions
- **C++20 Concepts**: Requires compiler support for concepts and requires expressions
- **IRStream**: Forward declared (full definition needed for verify_stream implementation)

## Thread Safety
- **All Methods**: Static and stateless, inherently thread-safe
- **verify_stream**: Requires external synchronization for IRStream access
- **No Shared State**: Compile-time constants and local operations only

## Performance Characteristics
- **Concept Checking**: Zero runtime cost, compile-time verification
- **Dispatch**: O(1) fold expression evaluation per node
- **Verification**: Inline noexcept functions, minimal branching
- **Memory**: No dynamic allocation, all static/const data
- **Cache**: Small functions likely to be inlined and cached
- **Scalability**: Variadic template supports arbitrary number of operations