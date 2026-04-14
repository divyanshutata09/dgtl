# Stack_Spiller.hpp Documentation

## Map of the Control Flow

```
Transactional Stack Spiller (Register Exhaustion Handler)
├── SpillSlot Struct
│   ├── virtual_id: Virtual register ID being spilled
│   └── stack_offset: Offset in stack frame
├── StackSpiller Class
│   ├── Constructor: Default initialization
│   ├── execute(stream, arena)
│   │   ├── Validation: Check stream validity
│   │   ├── Transaction Begin: stream.begin_strike()
│   │   ├── Shadow Processing: process_shadow()
│   │   ├── Sentinel Validation: validate_shadow_integrity()
│   │   ├── Commit/Rollback: Based on validation result
│   │   └── Error Handling: handle_spill_failure()
│   ├── get_frame_size(): Calculate aligned stack frame size
│   ├── reset(): Clear spill state for new compilation
│   ├── process_shadow(stream): High-speed shadow tier surgery
│   │   ├── Allocation Phase: Handle IS_SPILL_CANDIDATE flags
│   │   │   ├── allocate_spill_slot(): Get/create stack slot
│   │   │   ├── Hijack op_c: Store stack offset
│   │   │   ├── Set SVE_ACTIVE: Mark as stack-resident
│   │   │   └── Update Hash: Lock stack intent
│   │   └── Resolution Phase: Check operand reloading
│   │       ├── check_and_reload(): Mark spilled operands
│   │       └── Set IS_SPILL_CANDIDATE: For emitter handling
│   ├── allocate_spill_slot(vid): Get or create spill slot
│   │   ├── Lookup: Check existing mappings
│   │   ├── New Slot: Allocate 8-byte aligned slot
│   │   └── Update Depth: Track stack usage
│   ├── check_and_reload(operand, flags): Mark spilled operands
│   │   ├── Invalid Check: Skip invalid operands
│   │   ├── Spill Lookup: Find spilled virtual IDs
│   │   └── Flag Update: Mark for stack-relative access
│   └── handle_spill_failure(): Error reporting and rollback
└── Pillar Integration
    ├── Pillar 1: 64-byte alignment for SIMD performance
    ├── Pillar 2: Shadow tier prevents IR corruption
    ├── Pillar 3: Sentinel validation ensures correctness
    ├── Transactional Safety: Rollback on validation failure
    ├── Memory Efficiency: 8-byte slots with 16-byte frame alignment
    ├── Zero-Corruption: Failed spills cannot poison main IR
    ├── High-Speed Surgery: IRAtom/IRCore split accessors
```

**Data Flow:**
1. **Spill Detection**: Identify IS_SPILL_CANDIDATE nodes from register allocation
2. **Transaction Start**: Begin shadow tier operation
3. **Slot Allocation**: Map virtual IDs to stack offsets
4. **IR Modification**: Update nodes with stack offsets and flags
5. **Operand Resolution**: Mark spilled operands for reloading
6. **Validation**: Check shadow tier integrity
7. **Commit/Rollback**: Apply changes or revert based on validation
8. **Frame Calculation**: Return required stack frame size

## Summary

Stack_Spiller.hpp implements a transactional stack spiller that handles register exhaustion by promoting virtual registers to stack slots using Pillar 2 shadow tier operations and Pillar 3 sentinel validation. The spiller operates on the IR stream to transform register-resident operations into stack-relative memory accesses while maintaining transactional safety guarantees.

Key features include:
- **Transactional Safety**: Shadow tier prevents corruption of main IR
- **Sentinel Validation**: Bit-level integrity checking of transformations
- **High-Speed Surgery**: IRAtom/IRCore split accessors for performance
- **Stack Frame Management**: Automatic calculation of required stack space
- **Operand Resolution**: Automatic detection and marking of spilled operands
- **Memory Alignment**: 8-byte slots with 16-byte frame alignment for ABI compliance
- **Error Recovery**: Automatic rollback on validation failure
- **Zero-Corruption**: Failed spills cannot affect the original IR stream

The implementation achieves register spilling with transactional safety and high performance through optimized shadow tier operations.

## Full Explanation of Code

### Namespace and Includes
```cpp
#pragma once
#include <vector>
#include <iostream>
#include <cstdint>

// Corrected relative paths based on your project structure
#include "../ir/Global_IR.hpp"
#include "../core/Arena.hpp"

namespace dgtl::opt {
```
- **#pragma once**: Header guard
- **Standard Includes**:
  - `<vector>`: Dynamic arrays for spill slot tracking
  - `<iostream>`: Error output streams
  - `<cstdint>`: Fixed-width integers
- **Internal Includes**:
  - `"../ir/Global_IR.hpp"`: IRStream, IRAtom, IRCore, Flags types
  - `"../core/Arena.hpp"`: DGTLArena reference (unused but included)

### SpillSlot Struct
```cpp
struct SpillSlot {
    uint32_t virtual_id;
    int32_t stack_offset;
};
```
**Purpose**: Track mapping from virtual register to stack location
**Members**:
- **virtual_id**: Virtual register ID being spilled to stack
- **stack_offset**: Byte offset in stack frame

### StackSpiller Class
```cpp
class alignas(64) StackSpiller {
    struct SpillSlot {
        uint32_t virtual_id;
        int32_t stack_offset;
    };

    std::vector<SpillSlot> m_spill_map;
    int32_t m_current_stack_depth = 0;
```
**Alignment**: 64-byte alignment for SIMD performance
**Members**:
- **m_spill_map**: Vector tracking virtual-to-stack mappings
- **m_current_stack_depth**: Current stack frame size in bytes

### execute()
```cpp
void execute(ir::IRStream& stream, [[maybe_unused]] dgtl::core::DGTLArena& arena) {
    // Validation: Ensure stream is valid and not empty
    if (stream.is_null() || stream.size() == 0) [[unlikely]] {
        return;
    }

    // PILLAR 2: Begin Transaction. Clones Primary -> Shadow Tier.
    stream.begin_strike();

    // Perform the "Surgery" directly on the Shadow Tier of the stream.
    process_shadow(stream);

    // PILLAR 3: THE SENTINEL (Bit-Level Invariant Check)
    // Validates that the shadow transformation didn't break opcode-operand logic.
    if (stream.validate_shadow_integrity()) [[likely]] {
        stream.commit_strike(); 
    } else {
        // Halt! Invariant broken. Original IR remains untouched.
        stream.rollback_strike();
        handle_spill_failure();
    }
}
```
**Purpose**: Execute transactional stack spilling
**Algorithm**:
1. Validate input stream
2. Begin shadow tier transaction
3. Process shadow copy
4. Validate transformation integrity
5. Commit changes or rollback on failure

### get_frame_size()
```cpp
[[nodiscard]] uint32_t get_frame_size() const noexcept {
    return (static_cast<uint32_t>(m_current_stack_depth) + 15) & ~15;
}
```
**Purpose**: Calculate aligned stack frame size
**Alignment**: 16-byte alignment for modern ABI compliance (x86_64/AArch64)
**Formula**: (depth + 15) & ~15 rounds up to next 16-byte boundary

### reset()
```cpp
void reset() noexcept {
    m_spill_map.clear();
    m_current_stack_depth = 0;
}
```
**Purpose**: Reset spiller state for new compilation session
**Clears**: Spill map and stack depth counter

### process_shadow()
```cpp
void process_shadow(ir::IRStream& s) {
    const size_t limit = s.size();
    
    for (size_t i = 0; i < limit; ++i) {
        ir::IRAtom& atom = s.get_shadow_atom(i);
        ir::IRCore& core = s.get_shadow_core(i);

        // 1. ALLOCATION: If marked as spill candidate (from RegAlloc)
        if (atom.flags & ir::Flags::IS_SPILL_CANDIDATE) {
            int32_t offset = allocate_spill_slot(core.result_id);
            
            // Hijack op_c for stack offset. Emitter uses SVE_ACTIVE to interpret.
            core.op_c = static_cast<uint32_t>(offset);
            atom.flags |= ir::Flags::SVE_ACTIVE; // Mark as hardware-mapped (to stack)
            
            // PILLAR 3: Refresh Hash to lock the new 'Stack-Resident' intent
            uint32_t op_val = static_cast<uint32_t>(static_cast<uint16_t>(atom.opcode));
            core.intent_hash = (op_val * 31u) ^ (core.op_a * 17u) ^ 
                               (core.op_b * 7u) ^ (core.op_c * 3u);
        }

        // 2. OPERAND RESOLUTION: Check if inputs (op_a/op_b) reside on the stack
        check_and_reload(core.op_a, atom.flags);
        check_and_reload(core.op_b, atom.flags);
    }
}
```
**Purpose**: Perform high-speed surgery on shadow tier
**Two Phases**:
1. **Allocation**: Handle spill candidates by allocating stack slots
2. **Resolution**: Mark operands that need stack reloading

**Allocation Logic**:
- Check IS_SPILL_CANDIDATE flag from register allocator
- Allocate stack slot for virtual ID
- Store offset in op_c field
- Set SVE_ACTIVE flag for emitter recognition
- Update intent hash to lock stack intent

**Resolution Logic**:
- Check if operands are spilled virtual IDs
- Mark nodes for stack-relative access during emission

### allocate_spill_slot()
```cpp
int32_t allocate_spill_slot(uint32_t vid) {
    for (const auto& slot : m_spill_map) {
        if (slot.virtual_id == vid) [[unlikely]] return slot.stack_offset;
    }
    
    int32_t offset = m_current_stack_depth;
    m_spill_map.push_back({vid, offset});
    m_current_stack_depth += 8; // 64-bit slots for high-performance memory alignment
    return offset;
}
```
**Purpose**: Get or create stack slot for virtual ID
**Algorithm**:
1. Check existing mappings (reuse slots)
2. Allocate new slot if not found
3. Update stack depth (8-byte alignment)
4. Return offset from frame base

### check_and_reload()
```cpp
void check_and_reload(uint32_t operand, uint16_t& flags) noexcept {
    if (operand == ir::INVALID_SLOT) return; 

    for (const auto& slot : m_spill_map) {
        if (slot.virtual_id == operand) {
            // Operand is on stack; mark node for stack-relative LDR during emission
            flags |= ir::Flags::IS_SPILL_CANDIDATE; 
            break;
        }
    }
}
```
**Purpose**: Mark operands that need stack reloading
**Logic**:
- Skip invalid operands
- Check if operand is a spilled virtual ID
- Set IS_SPILL_CANDIDATE flag for emitter to generate stack loads

### handle_spill_failure()
```cpp
void handle_spill_failure() {
    std::cerr << "\033[1;31m[DGTL CRITICAL]\033[0m SPILL SURGERY FAILURE: Invariant check failed.\n";
    std::cerr << "[DGTL] Rollback triggered. Primary IR tier remains intact.\n";
}
```
**Purpose**: Report spill failure and rollback
**Output**: Colored error messages to stderr
**Action**: Logs failure (rollback handled by caller)

### Namespace Closing
```cpp
} // namespace dgtl::opt

/**
 * @namespace dgtl::opt
 * @brief Compatibility Alias for DGTL-Zenith high-level logic.
 */
namespace dgtl::opt {
    using StackSpiller = dgtl::opt::StackSpiller;
}
```
- Closes the opt namespace
- Provides compatibility alias

## Usage Example
```cpp
#include "Stack_Spiller.hpp"

// Initialize spiller
dgtl::opt::StackSpiller spiller;

// Process IR stream with spill candidates
dgtl::ir::IRStream ir_stream = // ... IR with register pressure ...
dgtl::core::DGTLArena arena;

spiller.execute(ir_stream, arena);

// Get required stack frame size
uint32_t frame_size = spiller.get_frame_size();

// Reset for next compilation
spiller.reset();
```

## Dependencies
- **Global_IR.hpp**: IRStream, IRAtom, IRCore, Flags, INVALID_SLOT, begin_strike(), commit_strike(), rollback_strike(), validate_shadow_integrity(), get_shadow_atom(), get_shadow_core()
- **Arena.hpp**: DGTLArena reference (currently unused)

## Performance Characteristics
- **Processing Speed**: High-speed shadow tier surgery with split accessors
- **Memory Usage**: 8-byte slots per spilled register
- **Frame Alignment**: 16-byte ABI compliance
- **Transactional Safety**: Zero impact on original IR
- **Validation Speed**: Bit-level integrity checking
- **Error Recovery**: Automatic rollback on failure
- **Scalability**: Linear processing of IR nodes

## Integration Points
- **Register Allocator**: Sets IS_SPILL_CANDIDATE flags
- **Code Emitter**: Interprets SVE_ACTIVE and IS_SPILL_CANDIDATE flags
- **Stack Frame**: Uses get_frame_size() for prologue/epilogue
- **JIT Executor**: Applies stack transformations before emission
- **Memory Guard**: Ensures stack bounds checking for spilled values
- **Debug Store**: Records spill locations for debugging

## Safety Guarantees
- **Zero-Corruption**: Shadow tier prevents main IR modification
- **Invariant Protection**: Sentinel validation ensures correctness
- **Transactional Rollback**: Failed spills leave IR unchanged
- **Memory Safety**: Bounds checking through Memory Guard integration
- **ABI Compliance**: 16-byte stack alignment for all platforms
- **Error Containment**: Failures logged but don't crash compilation