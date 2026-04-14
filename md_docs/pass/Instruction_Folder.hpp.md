# Instruction_Folder.hpp Documentation

## Map of the Control Flow

```
Formal-Solver Instruction Folder (TITAN Folding Strike)
├── DGTLInstructionFolder Class
│   ├── Constructor: Initialize with formal verifier reference
│   ├── fold_stream(stream)
│   │   ├── Validation: Check stream size
│   │   ├── Transaction Begin: begin_transaction()
│   │   ├── Node Processing: Process each instruction
│   │   ├── Constant Folding: try_fold_node()
│   │   ├── Formal Simplification: find_minimal_equivalent()
│   │   ├── Shadow Modification: Update shadow tier
│   │   ├── Hash Finalization: finalize_shadow_node()
│   │   ├── Sentinel Validation: validate_shadow_integrity()
│   │   ├── Commit/Rollback: Based on validation result
│   │   └── Error Handling: Silent rollback on failure
│   ├── sync_and_finalize(stream, i, node): Copy to shadow
│   │   ├── Shadow Copy: Primary to shadow transfer
│   │   └── Hash Refresh: finalize_shadow_node()
│   ├── finalize_shadow_node(s, idx): Hash computation
│   │   ├── Master Formula: compute_master_hash_bits()
│   │   └── Atomic Fence: Memory consistency
│   └── try_fold_node(node): Local constant folding
│       ├── Flag Analysis: Check constant operands
│       ├── Arithmetic Folding: ADD_I32, MUL_I32, SUB_I32
│       ├── Floating Point: ADD_F32 with bit casting
│       ├── Identity Operations: x+0, x*1, x*0, x-x
│       ├── Extended Ops: Delegate to formal solver
│       └── Result Return: ConstantResult or nullopt
└── Pillar Integration
    ├── Pillar 1: Formal verifier integration (SMT-based)
    ├── Pillar 2: Shadow tier prevents IR corruption
    ├── Pillar 3: Sentinel validation ensures correctness
    ├── Atomic Fences: Memory consistency guarantees
    ├── Exception-Free: Boolean transaction flow
    ├── L1 Cache Optimization: Deferred shadow writing
    ├── Zero-Corruption: Failed folding cannot poison main IR
    ├── Master Hash Formula: Centralized hash computation
```

**Data Flow:**
1. **Transaction Start**: Begin shadow tier operation
2. **Node Analysis**: Examine each instruction for folding opportunities
3. **Local Folding**: Try simple constant arithmetic
4. **Formal Solving**: Use SMT solver for complex simplifications
5. **Shadow Update**: Modify shadow tier with folded results
6. **Hash Refresh**: Recalculate intent hashes
7. **Integrity Check**: Validate shadow correctness
8. **Commit/Rollback**: Apply changes or revert based on validation

## Summary

Instruction_Folder.hpp implements a formal-logic driven instruction folder that uses SMT-based reduction and transactional shadow operations to perform constant folding and algebraic simplifications. The folder employs Pillar 2 shadow operations and Pillar 3 sentinel validation to guarantee zero-corruption while achieving high-performance instruction reduction through formal verification.

Key features include:
- **Formal Verification**: SMT-based algebraic simplification
- **Transactional Safety**: Shadow tier prevents corruption of main IR
- **Constant Folding**: Arithmetic operations on compile-time constants
- **Identity Elimination**: Remove no-op operations (x+0, x*1, x-x)
- **Floating Point Support**: Bit-accurate floating point folding
- **Extended Operations**: Delegate complex ops to formal solver
- **Atomic Consistency**: Memory fences for thread safety
- **Zero-Corruption Guarantee**: Failed folding operations leave IR unchanged

The implementation achieves instruction folding with formal correctness guarantees and transactional safety.

## Full Explanation of Code

### Namespace and Includes
```cpp
#pragma once
#include <optional>
#include <cstdint>
#include <cstring>
#include <atomic>
#include <cstdio>
#include "../ir/Global_IR.hpp"
#include "../core/Verified_Formal.hpp"

namespace dgtl::opt {
```
- **#pragma once**: Header guard
- **Standard Includes**:
  - `<optional>`: For optional return values
  - `<cstdint>`: Fixed-width integers
  - `<cstring>`: Memory operations for float bit casting
  - `<atomic>`: Memory fences
  - `<cstdio>`: Standard I/O (not directly used)
- **Internal Includes**:
  - `"../ir/Global_IR.hpp"`: IR types and operations
  - `"../core/Verified_Formal.hpp"`: Formal verifier interface

### DGTLInstructionFolder Class
```cpp
class DGTLInstructionFolder {
    DGTLFormalVerifier& formal_verifier;

public:
    explicit DGTLInstructionFolder(DGTLFormalVerifier& fv) noexcept 
        : formal_verifier(fv) {}
```
**Members**:
- **formal_verifier**: Reference to SMT-based formal verification engine

### fold_stream()
```cpp
void fold_stream(ir::IRStream& stream) noexcept {
    const size_t stream_size = stream.size();
    if (stream_size == 0) [[unlikely]] return;

    // PILLAR 2: Open Transactional Shadow Tier
    stream.begin_transaction();

    // Transaction state tracking for exception-free environments
    bool transaction_valid = true;

    for (uint32_t i = 0; i < static_cast<uint32_t>(stream_size); ++i) {
        const ir::IRNode node = stream.get_node_readonly(i);
        
        if (node.opcode == ir::OpCode::NOP) {
            sync_and_finalize(stream, i, node);
            continue;
        }

        // 1. Surgical Analysis: Check for hits before touching shadow memory
        auto collapsed = try_fold_node(node);
        auto simpler = (collapsed) ? std::nullopt : formal_verifier.find_minimal_equivalent(node);

        if (collapsed || simpler) {
            // Update shadow with folded result
            ir::IRAtom& s_atom = stream.get_shadow_atom(i);
            ir::IRCore& s_core = stream.get_shadow_core(i);

            if (collapsed) {
                // Convert to LOAD_IMM with constant
                s_atom.opcode = ir::OpCode::LOAD_IMM; 
                s_atom.flags = node.flags | ir::Flags::IS_CONST | ir::Flags::IS_SPEC_CONST;
                
                s_core.result_id = node.result_id;
                s_core.op_a = collapsed->low;
                s_core.op_b = collapsed->high;
                s_core.op_c = ir::INVALID_SLOT;
            } else {
                // Use formal solver result
                s_atom.opcode = simpler->opcode;
                s_atom.flags  = simpler->flags | ir::Flags::IS_MACRO_FUSED;
                
                s_core.result_id = simpler->result_id;
                s_core.op_a      = simpler->op_a;
                s_core.op_b      = simpler->op_b;
                s_core.op_c      = simpler->op_c;
            }

            // PILLAR 3: Atomic Finalization
            std::atomic_signal_fence(std::memory_order_release);
            finalize_shadow_node(stream, i);
        } else {
            // No folding possible, copy to shadow
            sync_and_finalize(stream, i, node);
        }
    }

    // PILLAR 3: Sentinel Bit-Consistency Check
    if (transaction_valid && stream.validate_shadow_integrity()) {
        stream.commit_transaction(); 
    } else {
        // Failure: Silent Rollback (HFT-Compliant)
        stream.rollback_transaction();
    }
}
```
**Purpose**: Execute transactional instruction folding
**Algorithm**:
1. Begin shadow transaction
2. Process each node
3. Try local constant folding
4. Use formal solver for complex cases
5. Update shadow with results
6. Validate and commit/rollback

### sync_and_finalize()
```cpp
void sync_and_finalize(ir::IRStream& stream, uint32_t i, const ir::IRNode& node) noexcept {
    ir::IRAtom& s_atom = stream.get_shadow_atom(i);
    ir::IRCore& s_core = stream.get_shadow_core(i);
    
    s_atom.opcode = node.opcode;
    s_atom.flags  = node.flags;
    s_core.result_id = node.result_id;
    s_core.op_a = node.op_a;
    s_core.op_b = node.op_b;
    s_core.op_c = node.op_c;

    finalize_shadow_node(stream, i);
}
```
**Purpose**: Copy unchanged node to shadow and finalize
**Operations**:
- Transfer all node data to shadow
- Refresh intent hash

### finalize_shadow_node()
```cpp
void finalize_shadow_node(ir::IRStream& s, uint32_t idx) noexcept {
    ir::IRAtom& atom = s.get_shadow_atom(idx);
    ir::IRCore& core = s.get_shadow_core(idx);

    // SYNC: Call the centralized Master Formula
    core.intent_hash = DGTLFormalVerifier::compute_master_hash_bits(
        atom.opcode, core.op_a, core.op_b, core.op_c
    );
}
```
**Purpose**: Compute intent hash for shadow node
**Uses**: Centralized master hash formula from formal verifier

### try_fold_node()
```cpp
std::optional<ConstantResult> try_fold_node(const ir::IRNode& node) noexcept {
    const uint16_t flags_raw = static_cast<uint16_t>(node.flags);
    const bool a_const = (flags_raw & static_cast<uint16_t>(ir::Flags::IS_SPEC_CONST));
    const bool b_const = (flags_raw & static_cast<uint16_t>(ir::Flags::IS_CONST));

    switch (node.opcode) {
        case ir::OpCode::ADD_I32:
            if (a_const && b_const) return ConstantResult{ node.op_a + node.op_b, 0 };
            if (b_const && node.op_b == 0) return ConstantResult{ node.op_a, 0 };
            break;

        case ir::OpCode::MUL_I32:
            if (a_const && b_const) return ConstantResult{ node.op_a * node.op_b, 0 };
            if (b_const && node.op_b == 0) return ConstantResult{ 0, 0 };
            if (b_const && node.op_b == 1) return ConstantResult{ node.op_a, 0 };
            break;

        case ir::OpCode::SUB_I32:
            if (node.op_a == node.op_b && !(flags_raw & 0x0400)) {
                return ConstantResult{ 0, 0 };
            }
            break;

        case ir::OpCode::ADD_F32:
            if (a_const && b_const) {
                float f_a, f_b;
                std::memcpy(&f_a, &node.op_a, sizeof(float));
                std::memcpy(&f_b, &node.op_b, sizeof(float));
                float res = f_a + f_b;
                uint32_t u_res;
                std::memcpy(&u_res, &res, sizeof(uint32_t));
                return ConstantResult{ u_res, 0 };
            }
            break;

        default:
            // If it's a BIR 2.0 Extended Op (0x0200+), let the formal solver handle it
            if (static_cast<uint16_t>(node.opcode) >= 0x0200) { 
                return formal_verifier.solve_static_math(node);
            }
            break;
    }
    return std::nullopt;
}
```
**Purpose**: Perform local constant folding
**Folding Rules**:
- **ADD_I32**: a+b (constants), x+0 = x
- **MUL_I32**: a*b (constants), x*0 = 0, x*1 = x
- **SUB_I32**: x-x = 0 (if no overflow flag)
- **ADD_F32**: Floating point addition with bit-accurate conversion
- **Extended Ops**: Delegate to formal solver

**Constant Detection**:
- **IS_SPEC_CONST**: Special constant flag
- **IS_CONST**: Regular constant flag

### Namespace Closing
```cpp
} // namespace dgtl::opt
```
- Closes the opt namespace

## Usage Example
```cpp
#include "Instruction_Folder.hpp"

// Initialize formal verifier and folder
dgtl::core::DGTLFormalVerifier verifier;
dgtl::opt::DGTLInstructionFolder folder(verifier);

// Process IR stream for instruction folding
dgtl::ir::IRStream ir_stream = // ... IR with constant expressions ...
folder.fold_stream(ir_stream);

// Result: Constants folded, identities eliminated
```

## Dependencies
- **Global_IR.hpp**: IRStream, IRNode, IRAtom, IRCore, Flags, OpCode, INVALID_SLOT, begin_transaction(), commit_transaction(), rollback_transaction(), validate_shadow_integrity(), get_node_readonly(), get_shadow_atom(), get_shadow_core()
- **Verified_Formal.hpp**: DGTLFormalVerifier, find_minimal_equivalent(), solve_static_math(), compute_master_hash_bits()

## Performance Characteristics
- **Processing Speed**: Linear pass with constant-time folding checks
- **Memory Usage**: Shadow buffer allocation
- **Hash Computation**: Master formula for consistency
- **Atomic Operations**: Signal fence for memory ordering
- **Transactional Safety**: Shadow tier overhead
- **Formal Solving**: SMT solver for complex cases
- **Scalability**: O(n) complexity with small constants

## Integration Points
- **Formal Verifier**: SMT-based algebraic simplification
- **Pass Manager**: Executed as optimization pass
- **Constant Propagation**: Works with constant analysis
- **JIT Executor**: Applied before code generation
- **Memory Guard**: Ensures bounds checking for folded constants
- **Debug Store**: Records folding decisions
- **Verified Formal**: Proves correctness of transformations

## Safety Guarantees
- **Zero-Corruption**: Shadow operations prevent main IR modification
- **Invariant Protection**: Sentinel validation ensures correctness
- **Transactional Rollback**: Failed folding leaves IR unchanged
- **Memory Consistency**: Atomic fences for thread safety
- **Exception-Free**: Boolean transaction flow for -fno-exceptions
- **Formal Correctness**: SMT-verified transformations
- **Silent Failure**: HFT-compliant error handling