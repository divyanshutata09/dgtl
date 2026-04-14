# DGTL IR Global Header (Global_IR.hpp)

## Control Flow Map

```
[Entry Point: Global_IR.hpp]
    |
    +--> PART 1: IR_Types.hpp (Fundamental Types)
    |       - Opcodes, TypeEnums, Width-specifiers
    |
    +--> PART 2: IR_Traits.hpp (Meta-Programming Traits)
    |       - SFINAE/Concepts for IR validity
    |
    +--> PART 3: IR_Dialect_interface.hpp (Dialect Abstraction)
    |       - Rules for AI, HFT, Mobile App opcodes
    |
    +--> PART 4: IR_Node.hpp (Atomic Unit)
    |       - 128/256-bit BitPacked Node structure
    |
    +--> PART 5: IR_Storage.hpp (Memory Architecture)
    |       - Arena-backed storage logic
    |
    +--> PART 6: IR_StreamCloneTransn.hpp (Orchestrator)
            - IRStream and Transactional API
```

**Flow Description**: This header orchestrates the inclusion of IR components in a strict order to ensure C++26 template resolution. It forms a layered architecture from low-level types to high-level stream operations.

## Summary

`Global_IR.hpp` is the universal entry point for DGTL's BitPacked Intermediate Representation (BIR) infrastructure. It provides a high-performance, domain-specific IR designed for AI, High-Frequency Trading (HFT), and full-stack applications. The header enforces a critical inclusion order for template resolution and defines the namespace `dgtl::ir` as the core of the compiler's IR system.

Key features:
- Bit-packed data structures for memory efficiency
- Transactional operations with rollback support
- Multi-dialect support (AI, HFT, Mobile)
- Arena-based memory management
- SIMD-friendly alignment (64-byte)

## Full Explanation

### Architecture Overview
The DGTL IR is built on a 6-layer architecture, each layer building upon the previous:

1. **IR_Types.hpp**: Defines the fundamental enumerations and constants used throughout the IR. This includes:
   - OpCode enumerations (ADD_I32, MUL_F32, LOAD, STORE, etc.)
   - Type enumerations (I32, F32, V512 for SIMD)
   - Width specifiers for bit-level operations

2. **IR_Traits.hpp**: Provides compile-time meta-programming facilities using C++ concepts and SFINAE. This ensures:
   - Type safety for IR operations
   - Compile-time validation of node structures
   - Template specialization for different dialects

3. **IR_Dialect_interface.hpp**: Defines the abstraction layer for domain-specific operations. Supports:
   - AI dialect: Neural network operations, quantization
   - HFT dialect: Low-latency financial operations
   - Mobile dialect: Power-efficient operations

4. **IR_Node.hpp**: The core data structure representing a single IR instruction. Features:
   - 24-byte unified node (Tier 1: Atom, Tier 2: Core, Tier 3: Joined)
   - Bit-packed fields for opcodes, operands, flags
   - Intent-hash for speculative optimization validation
   - 64-byte alignment for SIMD operations

5. **IR_Storage.hpp**: Memory management layer providing:
   - Transactional arena allocation
   - Huge-page support (2MB pages) for performance
   - Shadow buffers for speculative execution
   - Memory protection and bounds checking

6. **IR_StreamCloneTransn.hpp**: The public API layer offering:
   - IRStream class for building and manipulating IR
   - Transactional operations (begin_transaction, commit_transaction)
   - Cloning and transformation utilities
   - Stream serialization and deserialization

### Key Classes and Functions

#### IRStream
The primary interface for working with IR:

```cpp
class IRStream {
public:
    IRStream(DGTLArena& arena, size_t capacity);
    void push_node(OpCode op, Flags flags, uint32_t result_id, uint32_t op_a, uint32_t op_b, uint32_t op_c);
    IRNode get_node(size_t index) const;
    size_t size() const;
    void begin_transaction();
    void commit_transaction();
    uint64_t generate_logic_hash() const;
};
```

- **push_node**: Adds a new instruction to the stream
- **get_node**: Retrieves a node by index
- **begin_transaction/commit_transaction**: Enables speculative execution with rollback
- **generate_logic_hash**: Creates a hash for optimization validation

#### OpCode Enumeration
Defines all supported operations:

- **Memory**: LOAD, STORE, LEA
- **Integer**: ADD_I32, SUB_I32, MUL_I32, DIV_I32
- **Float**: ADD_F32, SUB_F32, MUL_F32, DIV_F32, FMA_F32
- **SIMD**: GATHER_F32_512, VEC_ADD_F32_512, etc.
- **Control Flow**: BRANCH, RETURN, SYSCALL
- **Specialized**: PHI_NODE, GUARD_VALUE

#### Flags
Bit flags for node behavior:
- IS_SPEC_CONST: Marks speculative constants
- IS_DEAD: Dead code elimination marker
- INTENT_HASH_VALID: Validation flag

### Usage Example

```cpp
#include "dgtl/ir/Global_IR.hpp"

using namespace dgtl;

int main() {
    core::DGTLArena arena(1); // 1MB arena
    ir::IRStream stream(arena, 1024);
    
    stream.begin_transaction();
    stream.push_node(ir::OpCode::ADD_I32, ir::Flags::NONE, 1, 10, 20, 0);
    stream.push_node(ir::OpCode::MUL_I32, ir::Flags::NONE, 2, 1, 5, 0);
    stream.commit_transaction();
    
    // Stream now contains: r1 = 10 + 20; r2 = r1 * 5
    return 0;
}
```

### Performance Characteristics

- **Memory Density**: 24 bytes per node vs 64+ in traditional IR
- **Cache Efficiency**: 64-byte alignment enables AVX-512 operations
- **Transactional Overhead**: Minimal (<1% for typical workloads)
- **Hash Validation**: Sub-microsecond intent verification

### Integration with Compiler Pipeline

The IR integrates with:
- **Analysis**: Liveness analysis, dependency tracking
- **Passes**: GVN, DCE, SIMD legalization
- **Code Generation**: Multi-target emission (x86-64, ARM64, WASM)
- **JIT**: Runtime compilation with W^X protection

### Error Handling

Since DGTL uses zero-exception policy:
- Memory allocation failures call `std::abort()`
- Invalid operations are caught at compile-time via traits
- Runtime errors use error codes or logging

### Future Extensions

- Additional dialects for quantum computing
- Hardware-specific optimizations
- Distributed IR for multi-node compilation

This header serves as the foundation for DGTL's compiler infrastructure, providing a robust, efficient, and extensible IR system optimized for modern computing demands.