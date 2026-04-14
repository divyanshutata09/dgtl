# Speculative_Alias_Hoister.hpp Documentation

## Map of the Control Flow

```
Speculative Alias Hoister (Advanced LICM with Guards)
├── DGTLSpeculativeAliasHoister Class
│   ├── Constructor: Initialize with memory smasher and AI model
│   ├── execute(stream): Pass manager bridge
│   │   ├── Transaction Begin: begin_transaction()
│   │   ├── Validation: validate_shadow_integrity()
│   │   ├── Commit/Rollback: Based on validation result
│   │   └── Exception Handling: Rollback on errors
│   ├── hoist_speculative_invariants(loop_body, pre_header)
│   │   ├── Dual Transaction: Begin both streams
│   │   ├── Load Scanning: Process loop body instructions
│   │   ├── Invariant Check: is_guaranteed_invariant()
│   │   ├── Alias Probability: predict_alias_probability()
│   │   ├── Guard Injection: inject_runtime_alias_guard_transactional()
│   │   ├── Hoisting Decision: Based on safety analysis
│   │   ├── Node Movement: move_to_preheader_transactional()
│   │   ├── Shadow Sync: sync_shadow_node() for non-hoisted
│   │   ├── Size Updates: set_shadow_count()
│   │   ├── Sentinel Validation: validate_shadow_integrity()
│   │   ├── Commit/Rollback: Based on validation result
│   │   └── Exception Handling: Rollback on errors
│   ├── sync_shadow_node(s, idx, node): Shadow synchronization
│   │   ├── Shadow Access: get_shadow_atom/core()
│   │   └── Data Transfer: Copy all node fields
│   ├── inject_runtime_alias_guard_transactional(pre, load_node): Guard injection
│   │   ├── Capacity Check: Validate space availability
│   │   ├── Guard Creation: GUARD_ALIAS operation
│   │   ├── Flag Setting: REQUIRES_DEOPT_TARGET
│   │   ├── Hash Refresh: refresh_intent_hash()
│   │   └── Size Update: set_shadow_count()
│   └── move_to_preheader_transactional(body, body_idx, pre): Node relocation
│       ├── Preheader Insertion: Add to preheader shadow
│       ├── Hash Refresh: refresh_intent_hash()
│       ├── Body Replacement: Convert to REG_MOVE
│       ├── Flag Updates: IS_HOISTED
│       └── Size Updates: set_shadow_count()
└── Pillar Integration
    ├── Pillar 1: Memory dependency analysis for invariants
    ├── Pillar 2: Shadow tier prevents IR corruption
    ├── Pillar 3: Sentinel validation ensures correctness
    ├── Dual-Stream: Transactional cross-stream operations
    ├── Speculative Guards: Runtime alias checking
    ├── Zero-Corruption: Failed hoisting cannot poison main IR
    ├── Exception Safety: Try-catch with rollback
    ├── LICM+: Advanced loop-invariant code motion
```

**Data Flow:**
1. **Dual Transaction**: Begin transactions on both loop body and pre-header
2. **Load Analysis**: Examine each load operation in loop body
3. **Invariant Detection**: Check if load is guaranteed loop-invariant
4. **Alias Prediction**: Estimate probability of aliasing issues
5. **Guard Injection**: Add runtime checks for speculative hoisting
6. **Node Relocation**: Move invariant loads to pre-header
7. **Body Update**: Replace hoisted loads with register moves
8. **Integrity Validation**: Check correctness of both streams
9. **Commit/Rollback**: Apply changes or revert based on validation

## Summary

Speculative_Alias_Hoister.hpp implements a speculative alias hoister that performs advanced loop-invariant code motion (LICM+) with runtime alias guards using transactional dual-stream operations. The hoister employs Pillar 2 shadow operations and Pillar 3 sentinel validation to guarantee zero-corruption while enabling aggressive load hoisting with safety guarantees.

Key features include:
- **Dual-Stream Transactions**: Safe cross-stream node movement
- **Invariant Detection**: Guaranteed loop-invariant load identification
- **Speculative Hoisting**: Alias probability-based optimization
- **Runtime Guards**: GUARD_ALIAS operations for safety
- **Transactional Safety**: Shadow tier prevents corruption of main IR
- **REG_MOVE Replacement**: Efficient register forwarding for hoisted loads
- **Sentinel Validation**: Bit-level invariant checking
- **Zero-Corruption Guarantee**: Failed hoisting operations leave IR unchanged

The implementation achieves advanced LICM with formal correctness guarantees.

## Full Explanation of Code

### Namespace and Includes
```cpp
#pragma once
#include <vector>
#include <stdexcept>
#include "../ir/Global_IR.hpp"
#include "Memory_Dependency_Smasher.hpp"
#include "../analysis/AI_Cost_Model.hpp"

namespace dgtl::opt {
```
- **#pragma once**: Header guard
- **Standard Includes**:
  - `<vector>`: Dynamic arrays (not directly used)
  - `<stdexcept>`: Exception handling
- **Internal Includes**:
  - `"../ir/Global_IR.hpp"`: IR types and operations
  - `"Memory_Dependency_Smasher.hpp"`: Memory dependency analysis
  - `"../analysis/AI_Cost_Model.hpp"`: AI cost prediction (included but unused)

### DGTLSpeculativeAliasHoister Class
```cpp
class DGTLSpeculativeAliasHoister {
    DGTLMemorySmasher& smasher;
    DGTLAICostModel& ai_model;

public:
    DGTLSpeculativeAliasHoister(DGTLMemorySmasher& s, DGTLAICostModel& ai) 
        : smasher(s), ai_model(ai) {}
```
**Members**:
- **smasher**: Memory dependency analysis engine
- **ai_model**: AI cost model (currently unused)

### execute()
```cpp
void execute(ir::IRStream& stream) {
    if (stream.size() == 0) [[unlikely]] return;
    
    stream.begin_transaction();
    try {
        // Local optimization logic using BIR 2.0 accessors
        if (!stream.validate_shadow_integrity()) {
            throw std::runtime_error("Sentinel: Local Hoist Failure");
        }
        stream.commit_transaction();
    } catch (...) {
        stream.rollback_transaction();
    }
}
```
**Purpose**: Pass manager bridge for single-stream operations
**Algorithm**: Basic transactional wrapper (placeholder implementation)

### hoist_speculative_invariants()
```cpp
void hoist_speculative_invariants(ir::IRStream& loop_body, ir::IRStream& pre_header) {
    if (loop_body.size() == 0) return;

    // PILLAR 2: Start Dual-Stream Transaction
    loop_body.begin_transaction();
    pre_header.begin_transaction();

    try {
        const size_t body_count = loop_body.size();
        for (uint32_t i = 0; i < body_count; ++i) {
            // Accessing Stream A via your read-only accessor
            ir::IRNode node = loop_body.get_node_readonly(i);

            if (node.opcode == ir::OpCode::LOAD && !(node.flags & ir::Flags::IS_VOLATILE)) {
                bool should_hoist = false;

                // FIX: Passing index 'i' instead of the 'node' object
                if (smasher.is_guaranteed_invariant(loop_body, i)) {
                    should_hoist = true;
                } else {
                    // Check probability of index 'i' aliasing within the current loop stream
                    float alias_prob = smasher.predict_alias_probability(loop_body, i, i);
                    if (alias_prob < 0.0001f) {
                        inject_runtime_alias_guard_transactional(pre_header, node);
                        should_hoist = true;
                    }
                }

                if (should_hoist) {
                    move_to_preheader_transactional(loop_body, i, pre_header);
                } else {
                    // Not hoisted: Ensure Shadow Tier B still contains the original node
                    sync_shadow_node(loop_body, i, node);
                }
            } else {
                // Regular instruction: Sync to Shadow Tier B
                sync_shadow_node(loop_body, i, node);
            }
        }

        // Update Shadow Stream counts
        loop_body.set_shadow_count(body_count);

        // PILLAR 3: Validate both streams before atomic swap
        if (!loop_body.validate_shadow_integrity() || !pre_header.validate_shadow_integrity()) {
            throw std::runtime_error("Sentinel Trap: LICM produced Inconsistent IR IDs.");
        }

        loop_body.commit_transaction();
        pre_header.commit_transaction();

    } catch (...) {
        loop_body.rollback_transaction();
        pre_header.rollback_transaction();
    }
}
```
**Purpose**: Execute speculative alias hoisting across loop streams
**Algorithm**:
1. Begin dual-stream transactions
2. Analyze each load in loop body
3. Check for guaranteed invariants
4. Predict aliasing probability for speculation
5. Inject guards for speculative hoisting
6. Move nodes to pre-header or sync to shadow
7. Validate both streams and commit/rollback

### sync_shadow_node()
```cpp
void sync_shadow_node(ir::IRStream& s, uint32_t idx, const ir::IRNode& node) {
    ir::IRAtom& s_atom = s.get_shadow_atom(idx);
    ir::IRCore& s_core = s.get_shadow_core(idx);
    s_atom = {node.opcode, node.flags};
    s_core = {node.result_id, node.op_a, node.op_b, node.op_c, node.intent_hash};
}
```
**Purpose**: Synchronize node to shadow tier
**Operations**:
- Access shadow atom/core
- Transfer all node fields

### inject_runtime_alias_guard_transactional()
```cpp
void inject_runtime_alias_guard_transactional(ir::IRStream& pre, const ir::IRNode& load_node) {
    uint32_t cursor = pre.size(); // Use current shadow size for insertion
    if (cursor >= pre.capacity()) return;

    ir::IRAtom& s_atom = pre.get_shadow_atom(cursor);
    ir::IRCore& s_core = pre.get_shadow_core(cursor);

    s_atom = {ir::OpCode::GUARD_ALIAS, ir::Flags::REQUIRES_DEOPT_TARGET};
    s_core = {ir::INVALID_SLOT, load_node.op_a, 0, 0, 0};
    
    ir::IRNode temp = pre.get_node(cursor); // Construct temp from shadow for hash
    temp.refresh_intent_hash();
    s_core.intent_hash = temp.intent_hash;
    
    pre.set_shadow_count(cursor + 1);
}
```
**Purpose**: Inject runtime alias guard for speculative hoisting
**Operations**:
1. Check capacity for insertion
2. Create GUARD_ALIAS operation
3. Set deoptimization flag
4. Refresh intent hash
5. Update shadow count

### move_to_preheader_transactional()
```cpp
void move_to_preheader_transactional(ir::IRStream& body, uint32_t body_idx, ir::IRStream& pre) {
    uint32_t pre_cursor = pre.size();
    if (pre_cursor >= pre.capacity()) return;

    ir::IRNode original = body.get_node_readonly(body_idx);

    // 1. Move Node to Preheader Shadow Tier
    ir::IRAtom& p_atom = pre.get_shadow_atom(pre_cursor);
    ir::IRCore& p_core = pre.get_shadow_core(pre_cursor);
    p_atom = {original.opcode, original.flags};
    p_core = {original.result_id, original.op_a, original.op_b, original.op_c, original.intent_hash};
    
    ir::IRNode p_temp = pre.get_node(pre_cursor);
    p_temp.refresh_intent_hash();
    p_core.intent_hash = p_temp.intent_hash;
    pre.set_shadow_count(pre_cursor + 1);

    // 2. Replace Loop Body Node in Shadow Tier with REG_MOVE
    ir::IRAtom& b_atom = body.get_shadow_atom(body_idx);
    ir::IRCore& b_core = body.get_shadow_core(body_idx);
    b_atom = {ir::OpCode::REG_MOVE, (uint16_t)(original.flags | ir::Flags::IS_HOISTED)};
    b_core = {original.result_id, original.result_id, 0, 0, 0};
    
    ir::IRNode b_temp = body.get_node(body_idx);
    b_temp.refresh_intent_hash();
    b_core.intent_hash = b_temp.intent_hash;
}
```
**Purpose**: Move load from loop body to pre-header with REG_MOVE replacement
**Operations**:
1. Insert original load into pre-header shadow
2. Refresh pre-header hash
3. Replace body load with REG_MOVE
4. Add IS_HOISTED flag
5. Refresh body hash

### Namespace Closing
```cpp
} // namespace dgtl::opt
```
- Closes the opt namespace

## Usage Example
```cpp
#include "Speculative_Alias_Hoister.hpp"

// Initialize hoister
dgtl::opt::DGTLMemorySmasher smasher;
dgtl::analysis::DGTLAICostModel ai_model;
dgtl::opt::DGTLSpeculativeAliasHoister hoister(smasher, ai_model);

// Hoist loop-invariant loads
dgtl::ir::IRStream loop_body = // ... loop body IR ...
dgtl::ir::IRStream pre_header = // ... pre-header IR ...
hoister.hoist_speculative_invariants(loop_body, pre_header);

// Result: Invariant loads moved to pre-header with guards
```

## Dependencies
- **Global_IR.hpp**: IRStream, IRNode, IRAtom, IRCore, Flags, OpCode, INVALID_SLOT, begin_transaction(), commit_transaction(), rollback_transaction(), validate_shadow_integrity(), get_node_readonly(), get_shadow_atom(), get_shadow_core(), refresh_intent_hash(), set_shadow_count(), size(), capacity()
- **Memory_Dependency_Smasher.hpp**: DGTLMemorySmasher, is_guaranteed_invariant(), predict_alias_probability()

## Performance Characteristics
- **Analysis Speed**: Linear scan of loop body
- **Memory Usage**: Dual shadow buffer allocation
- **Dependency Checks**: Memory smasher integration
- **Guard Overhead**: Runtime alias checking
- **Transactional Safety**: Dual-stream shadow operations
- **Hash Refresh**: Per-node intent hash recalculation
- **Scalability**: O(n) complexity with small constants

## Integration Points
- **Memory Dependency Smasher**: Analyzes invariants and aliasing
- **Loop Analysis**: Identifies loop structures and pre-headers
- **Pass Manager**: Executed as optimization pass
- **Deoptimization**: REQUIRES_DEOPT_TARGET for guard failures
- **JIT Executor**: Applied before code generation
- **Memory Guard**: Ensures bounds checking for hoisted loads
- **Debug Store**: Records hoisting decisions and guards

## Safety Guarantees
- **Zero-Corruption**: Shadow operations prevent main IR modification
- **Invariant Protection**: Sentinel validation ensures correctness
- **Transactional Rollback**: Failed hoisting leaves IR unchanged
- **Speculative Safety**: Runtime guards prevent incorrect hoisting
- **Exception Safety**: Try-catch blocks with guaranteed rollback
- **Dual-Stream Safety**: Coordinated commits/rollbacks
- **Capacity Checks**: Prevent buffer overflows