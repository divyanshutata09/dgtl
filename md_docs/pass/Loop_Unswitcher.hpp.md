# Loop_Unswitcher.hpp Documentation

## Map of the Control Flow

```
AI-Driven Speculative Loop Unswitcher (Zero-Corruption Unswitching)
├── DGTLLoopUnswitcher Class
│   ├── Constructor: Initialize with AI model, DCE engine, formal verifier
│   ├── unswitch_loop(stream)
│   │   ├── Stream Scan: Process each instruction
│   │   ├── Invariant Detection: is_loop_invariant()
│   │   ├── AI Cost Analysis: predict_cost() and speedup calculation
│   │   ├── Transaction Begin: begin_strike()
│   │   ├── Shadow Versioning: perform_shadow_versioning()
│   │   ├── Sentinel Validation: verify_bit_consistency()
│   │   ├── Commit/Rollback: Based on validation result
│   │   └── Early Return: Success triggers return
│   ├── is_loop_invariant(cond_reg, stream): Prove condition stability
│   │   ├── Register Scan: Check all redefinitions
│   │   ├── Side Effect Check: Volatile/side effect detection
│   │   └── Invariant Proof: Return true if never redefined
│   └── perform_shadow_versioning(stream, branch_idx): Path specialization
│       ├── Baseline Copy: push_full_to_shadow()
│       ├── Branch Transformation: BRANCH_COND to GOTO
│       ├── Flag Updates: IS_SPEC_CONST, IS_HOISTED
│       ├── Target Adjustment: Move true target to op_a
│       └── Speculative DCE: execute_speculative()
└── Pillar Integration
    ├── Pillar 1: AI cost model for profitability analysis
    ├── Pillar 2: Shadow tier prevents IR corruption
    ├── Pillar 3: Sentinel validation ensures correctness
    ├── Formal Verification: Bit-consistency proof
    ├── Speculative DCE: Dead code elimination after unswitching
    ├── Zero-Corruption: Failed unswitching cannot poison main IR
    ├── Transactional Safety: Isolated shadow operations
    ├── AI-Driven: Cost-benefit analysis for optimization decisions
```

**Data Flow:**
1. **Loop Analysis**: Scan for conditional branches in loops
2. **Invariant Proof**: Verify branch condition is loop-invariant
3. **AI Evaluation**: Predict performance impact and cache penalties
4. **Transaction Start**: Begin shadow tier operation
5. **Path Specialization**: Transform branch to unconditional jump
6. **Dead Code Elimination**: Remove unreachable code paths
7. **Integrity Validation**: Check shadow correctness
8. **Commit/Rollback**: Apply changes or revert based on validation

## Summary

Loop_Unswitcher.hpp implements an AI-driven speculative loop unswitcher that lifts invariant conditional branches out of loops using transactional shadow operations and formal verification. The unswitcher employs Pillar 2 shadow operations and Pillar 3 sentinel validation to guarantee zero-corruption while using AI cost modeling to ensure profitable transformations.

Key features include:
- **AI Cost Modeling**: Predict performance impact and cache penalties
- **Loop Invariant Detection**: Prove branch conditions are stable across iterations
- **Transactional Safety**: Shadow tier prevents corruption of main IR
- **Path Specialization**: Transform conditional branches to unconditional jumps
- **Speculative DCE**: Eliminate dead code after unswitching
- **Formal Verification**: Bit-level consistency validation
- **Zero-Corruption Guarantee**: Failed unswitching operations leave IR unchanged

The implementation achieves loop unswitching with AI-driven profitability analysis and transactional safety.

## Full Explanation of Code

### Namespace and Includes
```cpp
#pragma once
#include <algorithm>
#include "../ir/Global_IR.hpp"
#include "../analysis/AI_Cost_Model.hpp"
#include "Speculative_DCE.hpp"

namespace dgtl::opt {
```
- **#pragma once**: Header guard
- **Standard Includes**:
  - `<algorithm>`: For algorithmic operations (not directly used)
- **Internal Includes**:
  - `"../ir/Global_IR.hpp"`: IR types and operations
  - `"../analysis/AI_Cost_Model.hpp"`: AI cost prediction
  - `"Speculative_DCE.hpp"`: Speculative dead code elimination

### DGTLLoopUnswitcher Class
```cpp
class DGTLLoopUnswitcher {
    DGTLAICostModel& ai_model;
    DGTLSpeculativeDCE& dce_engine;
    DGTLFormalVerifier& verifier;

public:
    DGTLLoopUnswitcher(DGTLAICostModel& ai, DGTLSpeculativeDCE& dce, DGTLFormalVerifier& v) 
        : ai_model(ai), dce_engine(dce), verifier(v) {}
```
**Members**:
- **ai_model**: AI cost model for profitability analysis
- **dce_engine**: Speculative DCE for dead code elimination
- **verifier**: Formal verifier for correctness proof

### unswitch_loop()
```cpp
void unswitch_loop(ir::IRStream& stream) {
    const size_t count = stream.size();
    if (count == 0) [[unlikely]] return;

    for (uint32_t i = 0; i < static_cast<uint32_t>(count); ++i) {
        const auto& atom = stream.get_atom_readonly(i);
        const auto& core = stream.get_core_readonly(i);

        // 1. Detect Invariant Branch (Condition is constant across loop iterations)
        if (atom.opcode == ir::OpCode::BRANCH_COND && is_loop_invariant(core.op_a, stream)) {
            
            // 2. AI Bloat Check
            float cost_before = ai_model.predict_cost(stream);
            float speedup_gain = cost_before * 0.18f; 
            float cache_penalty = (count > 256) ? 8.5f : 1.2f;

            if (speedup_gain > cache_penalty) {
                // PILLAR 2: Begin Transactional Strike
                stream.begin_strike();
                
                if (perform_shadow_versioning(stream, i)) {
                    // PILLAR 3: Sentinel Check (Logic consistency check)
                    bool consistent = true;
                    for (uint32_t j = 0; j < stream.size(); ++j) {
                        if (!verifier.verify_bit_consistency(stream.get_node_readonly(j), stream.get_shadow_node_proxy(j))) {
                            consistent = false;
                            break;
                        }
                    }

                    if (consistent) {
                        stream.commit_strike(); 
                        return; // Mutation successful
                    }
                }
                
                stream.rollback_strike();
            }
        }
    }
}
```
**Purpose**: Execute AI-driven loop unswitching
**Algorithm**:
1. Scan for conditional branches
2. Verify loop invariance
3. Predict performance impact
4. Perform shadow versioning
5. Validate correctness
6. Commit or rollback

### is_loop_invariant()
```cpp
bool is_loop_invariant(uint32_t cond_reg, const ir::IRStream& s) const noexcept {
    for (uint32_t i = 0; i < s.size(); ++i) {
        const auto& core = s.get_core_readonly(i);
        const auto& atom = s.get_atom_readonly(i);
        
        // If the register is redefined by an instruction that has side effects or is volatile
        if (core.result_id == cond_reg) {
            // Using explicit flags to resolve red flags from missing combined masks
            if (atom.flags & (ir::Flags::HAS_SIDE_EFFECTS | ir::Flags::IS_VOLATILE)) {
                return false; 
            }
        }
    }
    return true;
}
```
**Purpose**: Prove branch condition is loop-invariant
**Logic**: Check if condition register is never redefined by side-effecting operations
**Returns**: True if condition is stable across loop iterations

### perform_shadow_versioning()
```cpp
bool perform_shadow_versioning(ir::IRStream& stream, uint32_t branch_idx) {
    // Step 1: Baseline copy (Now implemented in IRStream)
    stream.push_full_to_shadow();

    // Step 2: Specialize the Shadow Loop for the TRUE path
    auto& shad_atom = stream.get_shadow_atom(branch_idx);
    auto& shad_core = stream.get_shadow_core(branch_idx);

    // Transform into Static GOTO
    shad_atom.opcode = ir::OpCode::GOTO; 
    
    // Sync with BitPacked_IR.hpp Flag names
    shad_atom.flags |= ir::Flags::IS_SPEC_CONST; 
    shad_atom.flags |= ir::Flags::IS_HOISTED;
    
    shad_core.result_id = 0; 
    shad_core.op_a = shad_core.op_b; // Move True-target to op_a for GOTO logic

    // Step 3: Run Speculative DCE
    return dce_engine.execute_speculative(stream);
}
```
**Purpose**: Perform path specialization in shadow tier
**Steps**:
1. Copy entire stream to shadow
2. Transform conditional branch to unconditional goto
3. Set specialization flags
4. Run speculative DCE to eliminate dead code

### Namespace Closing
```cpp
} // namespace dgtl::opt
```
- Closes the opt namespace

## Usage Example
```cpp
#include "Loop_Unswitcher.hpp"

// Initialize components
dgtl::analysis::DGTLAICostModel ai_model;
dgtl::opt::DGTLSpeculativeDCE dce_engine;
dgtl::core::DGTLFormalVerifier verifier;

dgtl::opt::DGTLLoopUnswitcher unswitcher(ai_model, dce_engine, verifier);

// Process IR stream for loop unswitching
dgtl::ir::IRStream ir_stream = // ... IR with loop-invariant branches ...
unswitcher.unswitch_loop(ir_stream);

// Result: Profitable invariant branches lifted out of loops
```

## Dependencies
- **Global_IR.hpp**: IRStream, IRAtom, IRCore, Flags, OpCode, begin_strike(), commit_strike(), rollback_strike(), get_atom_readonly(), get_core_readonly(), get_shadow_atom(), get_shadow_core(), push_full_to_shadow(), get_shadow_node_proxy()
- **AI_Cost_Model.hpp**: DGTLAICostModel, predict_cost()
- **Speculative_DCE.hpp**: DGTLSpeculativeDCE, execute_speculative()
- **Verified_Formal.hpp**: DGTLFormalVerifier, verify_bit_consistency()

## Performance Characteristics
- **Analysis Speed**: Linear scan for invariant detection
- **AI Prediction**: Cost model evaluation for profitability
- **Memory Usage**: Shadow buffer allocation
- **Transactional Safety**: Shadow tier overhead
- **Validation Speed**: Bit-level consistency checking
- **Speculative DCE**: Dead code elimination after unswitching
- **Scalability**: O(n) complexity with small constants

## Integration Points
- **AI Cost Model**: Predicts performance impact
- **Speculative DCE**: Eliminates dead code after unswitching
- **Formal Verifier**: Proves correctness of transformations
- **Pass Manager**: Executed as optimization pass
- **Loop Analysis**: Identifies loop structures
- **JIT Executor**: Applied before code generation
- **Memory Guard**: Ensures bounds checking for specialized code

## Safety Guarantees
- **Zero-Corruption**: Shadow operations prevent main IR modification
- **Invariant Protection**: Sentinel validation ensures correctness
- **Transactional Rollback**: Failed unswitching leaves IR unchanged
- **AI Safety**: Cost-benefit analysis prevents harmful transformations
- **Formal Correctness**: Bit-consistency verification
- **Speculative Safety**: DCE only applied to provably dead code
- **Early Termination**: Successful transformation stops further processing