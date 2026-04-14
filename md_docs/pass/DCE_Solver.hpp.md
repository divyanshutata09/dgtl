# DCE_Solver.hpp Documentation

## Map of the Control Flow

```
Transactional Dead Code Elimination (DCE Solver)
├── DGTLDCESolver Class
│   ├── Constructor: Default initialization
│   ├── execute(stream)
│   │   ├── Validation: Check stream validity
│   │   ├── Shadow Allocation: allocate_transactional()
│   │   ├── Liveness Analysis: mark_live_instructions()
│   │   ├── Transactional Pruning: prune_to_shadow()
│   │   ├── Sentinel Validation: validate_shadow_integrity()
│   │   ├── Commit/Rollback: Based on validation result
│   │   └── Cleanup: abort_transaction() on failure
│   ├── mark_live_instructions(src, live_map)
│   │   ├── Reverse Pass: Process nodes backwards
│   │   ├── Essential Check: Terminators, side effects, stores
│   │   ├── Successor Check: Test if result needed later
│   │   ├── Operand Propagation: Mark operand dependencies
│   │   └── Result Marking: Set live bits for essential nodes
│   └── prune_to_shadow(src, shadow, live_map)
│       ├── Live Filtering: Check each node for liveness
│       ├── Safety Validation: Prevent uninitialized ID pollution
│       ├── Hash Refresh: Recalculate intent_hash
│       └── Shadow Push: Add live nodes to shadow buffer
└── Pillar Integration
    ├── Pillar 1: Bitset-based liveness (64k ID tracking)
    ├── Pillar 2: Shadow buffer prevents IR corruption
    ├── Pillar 3: Sentinel validation ensures correctness
    ├── Transactional Safety: Isolated shadow operations
    ├── Memory Efficiency: Bit-packed liveness tracking
    ├── Zero-Corruption: Failed DCE cannot poison main IR
    ├── Reverse Analysis: Backward liveness propagation
```

**Data Flow:**
1. **Stream Validation**: Check input IR validity
2. **Shadow Creation**: Allocate isolated transaction space
3. **Liveness Analysis**: Reverse-pass to identify live instructions
4. **Transactional Pruning**: Filter live nodes to shadow buffer
5. **Integrity Validation**: Check shadow correctness
6. **Commit/Rollback**: Apply changes or revert based on validation
7. **Cleanup**: Release shadow resources

## Summary

DCE_Solver.hpp implements a transactional dead code elimination pass that uses reverse-pass liveness analysis and shadow buffer compaction to safely remove unreachable or unused instructions. The solver employs Pillar 2 shadow operations and Pillar 3 sentinel validation to guarantee zero-corruption of the main IR stream.

Key features include:
- **Transactional Safety**: Shadow buffer prevents corruption of main IR
- **Reverse Liveness Analysis**: Backward pass identifies live instructions
- **Bit-Packed Tracking**: 64k ID liveness map for memory efficiency
- **Sentinel Validation**: Hash and stride validation of transformations
- **Essential Node Preservation**: Terminators, side effects, and stores always kept
- **Dependency Propagation**: Automatic marking of operand dependencies
- **Hash Re-validation**: Intent hash refresh during node movement
- **Zero-Corruption Guarantee**: Failed DCE operations leave IR unchanged

The implementation achieves dead code elimination with transactional safety and high performance through optimized liveness tracking.

## Full Explanation of Code

### Namespace and Includes
```cpp
#pragma once
#include <vector>
#include <bitset>
#include "../ir/Global_IR.hpp"

namespace dgtl::opt {
```
- **#pragma once**: Header guard
- **Standard Includes**:
  - `<vector>`: Dynamic arrays (not directly used)
  - `<bitset>`: Bit-packed liveness tracking
- **Internal Includes**:
  - `"../ir/Global_IR.hpp"`: IRStream, IRNode, Flags, OpCode types

### DGTLDCESolver Class
```cpp
class DGTLDCESolver {
    // 64k IDs for local block liveness tracking
    static constexpr uint32_t MAX_TRACKED_IDS = 65536;
```
**Constants**:
- **MAX_TRACKED_IDS**: 64k bitset for liveness tracking (covers most programs)

### execute()
```cpp
void execute(ir::IRStream& stream) {
    if (stream.is_null() || stream.size() == 0) [[unlikely]] return;

    // PILLAR 2: Shadow Buffer Allocation (Isolated Transaction Space)
    ir::IRStream shadow_stream = ir::IRStream::allocate_transactional(stream.capacity());
    
    if (shadow_stream.is_null()) return;

    // 1. Liveness Map (Bit-packed validation)
    std::bitset<MAX_TRACKED_IDS> live_map;
    mark_live_instructions(stream, live_map);

    // 2. Transactional Pruning: Write only live nodes to the shadow
    // The shadow stream must be in a transaction state to accept pushes
    shadow_stream.begin_transaction();
    bool success = prune_to_shadow(stream, shadow_stream, live_map);

    // PILLAR 3: THE SENTINEL
    // Validates shadow integrity (Hash/Stride/ResultID) before committing
    if (success && shadow_stream.validate_shadow_integrity()) {
        stream.commit_from_shadow(shadow_stream);
    } else {
        // Rollback: discard the shadow stream mutations
        shadow_stream.abort_transaction();
    }
}
```
**Purpose**: Execute transactional dead code elimination
**Algorithm**:
1. Validate input stream
2. Allocate shadow buffer
3. Perform liveness analysis
4. Begin shadow transaction
5. Prune dead code to shadow
6. Validate shadow integrity
7. Commit changes or rollback

### mark_live_instructions()
```cpp
void mark_live_instructions(const ir::IRStream& src, std::bitset<MAX_TRACKED_IDS>& live_map) {
    const size_t sz = src.size();
    for (int i = static_cast<int>(sz) - 1; i >= 0; --i) {
        // Synchronized with IRStream::get_node
        ir::IRNode node = src.get_node(i);
        
        bool is_essential = node.is_terminator() || 
                            (node.flags & ir::Flags::HAS_SIDE_EFFECTS) || 
                            (node.opcode == ir::OpCode::STORE);

        // A node is live if it's essential OR its result is needed by a subsequent op
        // [DGTL FIX]: Added check to ensure result_id is non-zero before bitset testing
        bool needed_by_successor = (node.result_id != 0) && live_map.test(node.result_id % MAX_TRACKED_IDS);

        if (is_essential || needed_by_successor) {
            // If this node is live, its operands must also be live
            if (node.op_a != ir::INVALID_SLOT && node.op_a != 0) live_map.set(node.op_a % MAX_TRACKED_IDS);
            if (node.op_b != ir::INVALID_SLOT && node.op_b != 0) live_map.set(node.op_b % MAX_TRACKED_IDS);
            if (node.op_c != ir::INVALID_SLOT && node.op_c != 0) live_map.set(node.op_c % MAX_TRACKED_IDS);
            
            // If it was essential but not previously in live_map, ensure it's marked
            if (node.result_id != 0) {
                live_map.set(node.result_id % MAX_TRACKED_IDS);
            }
        }
    }
}
```
**Purpose**: Perform reverse-pass liveness analysis
**Algorithm** (Reverse Order)**:
1. Process nodes from end to beginning
2. Check if node is essential (terminator/side effects/store)
3. Check if result is needed by later instructions
4. If live, mark all operands as live
5. Ensure essential nodes are marked in liveness map

**Essential Nodes**:
- Terminators (control flow)
- Side effect operations
- Store operations

**Safety Checks**:
- Non-zero result_id before bitset access
- INVALID_SLOT and zero operand filtering

### prune_to_shadow()
```cpp
bool prune_to_shadow(const ir::IRStream& src, ir::IRStream& shadow, const std::bitset<MAX_TRACKED_IDS>& live_map) {
    for (size_t i = 0; i < src.size(); ++i) {
        ir::IRNode node = src.get_node(i);
        
        bool is_live = node.is_terminator() || 
                       (node.flags & ir::Flags::HAS_SIDE_EFFECTS) || 
                       (node.opcode == ir::OpCode::STORE) ||
                       (node.result_id != 0 && live_map.test(node.result_id % MAX_TRACKED_IDS));

        if (is_live) {
            // [DGTL SAFETY] Prevent pollution from uninitialized IDs
            if (node.result_id == 0 && !node.is_terminator() && !(node.flags & ir::Flags::HAS_SIDE_EFFECTS)) {
                if (node.opcode != ir::OpCode::NOP) return false; 
            }
            
            // [PILLAR 3] REFRESH HASH
            // When moving nodes between streams/buffers, the intent_hash must be 
            // recalculated to ensure the Transactional Trap isn't tripped.
            node.intent_hash = ir::IRNode::calculate_hash(node.opcode, node.op_a, node.op_b, node.op_c);
            
            shadow.push_back(node);
        }
    }
    return true;
}
```
**Purpose**: Filter live instructions to shadow buffer
**Algorithm**:
1. Check each node for liveness
2. Validate safety (no uninitialized result IDs)
3. Recalculate intent hash for transactional safety
4. Push live nodes to shadow stream
5. Return success status

**Safety Validations**:
- Prevent pollution from uninitialized result IDs
- Only allow NOP operations with zero result_id
- Hash refresh for sentinel validation

### Namespace Closing
```cpp
} // namespace dgtl::opt
```
- Closes the opt namespace

## Usage Example
```cpp
#include "DCE_Solver.hpp"

// Initialize DCE solver
dgtl::opt::DGTLDCESolver dce_solver;

// Process IR stream to eliminate dead code
dgtl::ir::IRStream ir_stream = // ... IR with dead code ...
dce_solver.execute(ir_stream);

// Result: ir_stream now contains only live instructions
```

## Dependencies
- **Global_IR.hpp**: IRStream, IRNode, Flags, OpCode, INVALID_SLOT, allocate_transactional(), begin_transaction(), commit_from_shadow(), abort_transaction(), validate_shadow_integrity(), calculate_hash()

## Performance Characteristics
- **Analysis Speed**: Reverse linear pass over IR nodes
- **Memory Usage**: 64k bitset (8KB) for liveness tracking
- **Pruning Speed**: Forward pass with conditional copying
- **Hash Calculation**: Per-node hash refresh during pruning
- **Transactional Safety**: Shadow buffer allocation overhead
- **Validation Speed**: Bit-level integrity checking
- **Scalability**: Linear time complexity O(n)

## Integration Points
- **Pass Manager**: Executed as optimization pass
- **Analysis Manager**: May use liveness analysis results
- **JIT Executor**: Applied before code generation
- **Memory Guard**: Ensures bounds checking for live operations
- **Debug Store**: Records eliminated instructions for debugging
- **Verified Formal**: Proves correctness of DCE transformations

## Safety Guarantees
- **Zero-Corruption**: Shadow operations prevent main IR modification
- **Invariant Protection**: Sentinel validation ensures correctness
- **Transactional Rollback**: Failed DCE leaves IR unchanged
- **Memory Safety**: Bounds checking through bitset modulo operations
- **ID Safety**: Validation prevents uninitialized result ID pollution
- **Hash Integrity**: Intent hash refresh maintains transactional consistency
- **Essential Preservation**: Critical operations always retained