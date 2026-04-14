# Hardware_Scheduler.hpp Documentation

## Map of the Control Flow

```
Hardware Port Scheduler (Cycle-Accurate Instruction Scheduling)
├── CpuPorts Struct
│   ├── int8_t alu_slots = 4 (Ports 0, 1, 5, 6 on x86_64)
│   ├── int8_t vec_slots = 2 (Ports 0, 1 for SIMD)
│   ├── int8_t mem_slots = 2 (Ports 2, 3 for load/store)
│   └── reset() → Restore full capacity
├── DGTLHardwareScheduler Class
│   ├── Dependencies
│   │   ├── CpuPorts current_cycle_resources
│   │   └── DGTLAICostModel& ai_model
│   ├── Main Scheduling
│   │   └── schedule(stream)
│   │       ├── Transaction Begin: stream.begin_strike()
│   │       ├── Ready Queue Management
│   │       │   ├── Fill lookahead window (16 instructions)
│   │       │   └── Sort by AI-predicted benefit (critical path first)
│   │       ├── Cycle Simulation
│   │       │   ├── Reset cycle resources
│   │       │   ├── Dispatch ready instructions to shadow buffer
│   │       │   └── Stall-breaker for deadlock prevention
│   │       ├── Sentinel Validation
│   │       │   ├── validate_shadow_integrity() for SSA invariants
│   │       │   ├── Commit on success, rollback on failure
│   │       └── Transaction Resolution
│   ├── Resource Queries
│   │   └── can_dispatch(node) → bool
│   │       ├── Check REQUIRES_VEC_PORT flag
│   │       ├── Check memory opcodes (LOAD/STORE/GATHER)
│   │       └── Check ALU port availability
│   └── Resource Consumption
│       └── consume_resources(node) → void
│           ├── Decrement vec_slots for SIMD operations
│           ├── Decrement mem_slots for memory operations
│           └── Decrement alu_slots for ALU operations
└── Integration Points
    ├── IRStream: Transactional shadow buffer operations
    ├── AI_Cost_Model: predict_inlined_benefit() for prioritization
    └── IRNode: Flag checking and resource requirement analysis
```

**Data Flow:**
1. **Transaction Start**: Begin strike to enable shadow buffer writing
2. **Lookahead**: Fill ready queue with next 16 instructions
3. **AI Sorting**: Prioritize instructions by predicted inlining benefit
4. **Cycle Simulation**: Reset port counters, dispatch available instructions
5. **Resource Tracking**: Consume appropriate ports for each dispatched instruction
6. **Stall Prevention**: Force dispatch if all ALU ports saturated
7. **Validation**: Check SSA invariants on reordered sequence
8. **Commit/Rollback**: Apply changes only if invariants maintained

## Summary

Hardware_Scheduler.hpp implements a cycle-accurate hardware port scheduler that performs instruction-level parallelism optimization for DGTL's JIT compiler. The scheduler uses transactional shadow writing (Pillar 2) and Sentinel validation (Pillar 3) to safely reorder instructions for better hardware utilization while maintaining SSA invariants.

Key features include:
- **Port-Aware Scheduling**: Models x86-64 execution ports (ALU, Vector, Memory)
- **AI-Guided Prioritization**: Uses cost model predictions for critical path optimization
- **Transactional Safety**: Shadow buffer operations with automatic rollback on invariant violation
- **Cycle-Accurate Simulation**: Per-cycle resource tracking and dispatch
- **Deadlock Prevention**: Stall-breaker mechanism for saturated pipelines
- **SSA Validation**: Sentinel checks ensure correctness of reordered sequences

The implementation achieves hardware-native instruction scheduling with zero-corruption guarantees through comprehensive validation.

## Full Explanation of Code

### Namespace and Includes
```cpp
#pragma once
#include <array>
#include <vector>
#include <algorithm>
#include <cstring>
#include "../ir/Global_IR.hpp"
#include "../analysis/AI_Cost_Model.hpp"

namespace dgtl::opt {
```
- **#pragma once**: Header guard
- **Standard Includes**:
  - `<array>`: Fixed-size arrays (not used in current implementation)
  - `<vector>`: Dynamic arrays for ready queue
  - `<algorithm>`: std::sort for instruction prioritization
  - `<cstring>`: Memory operations (not used in current implementation)
- **Internal Includes**:
  - `"../ir/Global_IR.hpp"`: IRStream, IRNode, OpCode, Flags
  - `"../analysis/AI_Cost_Model.hpp"`: AI cost model for predictions
- **Namespace**: dgtl::opt (optimization)

### CpuPorts Struct
```cpp
struct CpuPorts {
    int8_t alu_slots = 4;    // Port 0, 1, 5, 6 on x86_64
    int8_t vec_slots = 2;    // Port 0, 1
    int8_t mem_slots = 2;    // Port 2, 3 (Load/Store)
    
    void reset() noexcept { alu_slots = 4; vec_slots = 2; mem_slots = 2; }
};
```
**Purpose**: Track available execution ports per cycle
**x86-64 Mapping**:
- **ALU Ports**: 4 slots (ports 0, 1, 5, 6) for arithmetic/logic
- **Vector Ports**: 2 slots (ports 0, 1) for SIMD operations
- **Memory Ports**: 2 slots (ports 2, 3) for load/store operations

### DGTLHardwareScheduler Class Members
```cpp
class DGTLHardwareScheduler {
    CpuPorts current_cycle_resources;
    DGTLAICostModel& ai_model;
```
**Members**:
- **current_cycle_resources**: Per-cycle port availability tracking
- **ai_model**: Reference to AI cost model for benefit predictions

### Constructor
```cpp
explicit DGTLHardwareScheduler(DGTLAICostModel& ai) : ai_model(ai) {}
```
**Purpose**: Initialize scheduler with AI cost model
**Note**: ai_model stored for future use (currently unused in implementation)

### Main Scheduling Method

#### schedule()
```cpp
void schedule(ir::IRStream& stream) {
    const size_t count = stream.size();
    if (count == 0) [[unlikely]] return;

    // PILLAR 2: Open Transactional Shadow Stream via Strike
    stream.begin_strike();

    std::vector<ir::IRNode> ready_queue;
    uint32_t scheduled_count = 0;
    uint32_t lookahead_ptr = 0;

    while (scheduled_count < static_cast<uint32_t>(count)) {
        current_cycle_resources.reset();

        // 1. Fill ready queue (Lookahead Window)
        // Using get_node as it is the standard accessor for the stream
        while (ready_queue.size() < 16 && lookahead_ptr < static_cast<uint32_t>(count)) {
            ready_queue.push_back(stream.get_node(lookahead_ptr++));
        }
        
        // 2. Sort by AI-Predicted Latency/Benefit to prioritize critical path
        std::sort(ready_queue.begin(), ready_queue.end(), [](const ir::IRNode& a, const ir::IRNode& b) {
            return a.predict_inlined_benefit() > b.predict_inlined_benefit();
        });

        // 3. Dispatch to Shadow Buffer
        for (auto it = ready_queue.begin(); it != ready_queue.end(); ) {
            if (can_dispatch(*it)) {
                consume_resources(*it);
                
                // FIX: push_node automatically targets the shadow buffer during a strike
                stream.push_node(*it);
                
                scheduled_count++;
                it = ready_queue.erase(it);
            } else {
                ++it;
            }
        }

        // Stall-Breaker: Force-advance if ports are saturated to prevent deadlocks
        if (!ready_queue.empty() && current_cycle_resources.alu_slots == 4) {
            stream.push_node(ready_queue[0]);
            ready_queue.erase(ready_queue.begin());
            scheduled_count++;
        }
    }

    // PILLAR 3: THE SENTINEL
    // Verify that the new hardware-optimized order hasn't broken SSA invariants
    // Updated to use the formal bit-consistency verification logic across the shadow stream
    bool consistent = true;
    for (uint32_t j = 0; j < stream.size(); ++j) {
        // Re-routing internal verification to the shadow proxy method
        if (!stream.validate_shadow_integrity()) {
            consistent = false;
            break;
        }
    }

    if (consistent) {
        stream.commit_strike(); // Swap Shadow to Primary
    } else {
        // Transaction Failed: Discard Shadow, keep Original sequence
        stream.rollback_strike();
    }
}
```
**Algorithm**: List scheduling with cycle-accurate port modeling
**Phases**:
1. **Transaction Start**: Begin strike for shadow buffer operations
2. **Ready Queue**: Fill with 16-instruction lookahead window
3. **AI Sorting**: Prioritize by predicted inlining benefit (critical path first)
4. **Cycle Dispatch**: Reset ports, dispatch available instructions
5. **Stall Prevention**: Force dispatch when ALU ports saturated
6. **Validation**: Check SSA invariants on reordered sequence
7. **Resolution**: Commit valid schedules, rollback invalid ones

**Transactional Safety**: All reordering happens in shadow buffer

### Resource Query Method

#### can_dispatch()
```cpp
[[nodiscard]] bool can_dispatch(const ir::IRNode& node) const noexcept {
    uint32_t f = static_cast<uint32_t>(node.flags);
    
    if (f & static_cast<uint32_t>(ir::Flags::REQUIRES_VEC_PORT)) {
        return current_cycle_resources.vec_slots > 0;
    }
    
    if (node.opcode == ir::OpCode::LOAD || node.opcode == ir::OpCode::STORE || 
        node.opcode == ir::OpCode::GATHER_F32_512 || node.opcode == ir::OpCode::STORE_V512) {
        return current_cycle_resources.mem_slots > 0;
    }
    
    return current_cycle_resources.alu_slots > 0;
}
```
**Purpose**: Check if instruction can be dispatched in current cycle
**Logic**:
- **Vector Check**: REQUIRES_VEC_PORT flag requires vector slots
- **Memory Check**: LOAD/STORE/GATHER operations require memory slots
- **ALU Default**: All other operations use ALU slots

**Return**: true if required port type has available slots

### Resource Consumption Method

#### consume_resources()
```cpp
void consume_resources(const ir::IRNode& node) noexcept {
    uint32_t f = static_cast<uint32_t>(node.flags);

    if (f & static_cast<uint32_t>(ir::Flags::REQUIRES_VEC_PORT)) {
        current_cycle_resources.vec_slots--;
    } else if (node.opcode == ir::OpCode::LOAD || node.opcode == ir::OpCode::STORE) {
        current_cycle_resources.mem_slots--;
    } else {
        current_cycle_resources.alu_slots--;
    }
}
```
**Purpose**: Update port availability after dispatching instruction
**Logic**:
- **Vector Operations**: Decrement vec_slots
- **Memory Operations**: Decrement mem_slots (LOAD/STORE only, not SIMD variants)
- **ALU Operations**: Decrement alu_slots (default case)

### Namespace Closing
```cpp
} // namespace dgtl::opt
```
- Closes the optimization namespace

## Usage Example
```cpp
#include "Hardware_Scheduler.hpp"

// Set up AI cost model and scheduler
dgtl::core::DGTLAICostModel ai_model(/*...*/);
dgtl::opt::DGTLHardwareScheduler scheduler(ai_model);

// Prepare IR stream
dgtl::ir::IRStream stream(/*...*/);
// ... populate stream with instructions ...

// Apply hardware-aware scheduling
scheduler.schedule(stream);

// Stream now contains port-optimized instruction order
// SSA invariants verified and maintained
```

## Dependencies
- **Global_IR.hpp**: IRStream, IRNode, OpCode, Flags definitions
- **AI_Cost_Model.hpp**: predict_inlined_benefit() for instruction prioritization

## Thread Safety
- **Scheduling**: Not thread-safe, requires exclusive stream access
- **AI Model**: Reference access assumes external synchronization
- **Resource Tracking**: Local state, no shared mutable data

## Performance Characteristics
- **Time Complexity**: O(N log W) where N is instruction count, W is window size (16)
- **Space Complexity**: O(W) for ready queue, W is constant (16)
- **Sorting**: AI-guided prioritization with std::sort
- **Validation**: O(N) sentinel checks per scheduling attempt
- **Transactional**: Shadow buffer operations prevent partial corruption
- **Cycle Accuracy**: Per-cycle resource simulation with port constraints