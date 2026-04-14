# ARM64_RegAlloc.hpp Documentation

## Map of the Control Flow

```
ARM64 Linear Scan Register Allocator
├── Register Constants (ARM64Regs)
│   ├── Scalar GPRs: 31 total (X0-X30), 29 allocatable
│   ├── Special Registers: XZR(31), SP(31), FP(29), LR(30)
│   ├── Vector Registers: 32 total (V0-V31), all allocatable
│   └── Allocation Masks: ALLOCABLE_MASK, VEC_ALLOCABLE_MASK
├── DGTLARM64RegAlloc Class
│   ├── State Management
│   │   ├── free_mask (uint32_t): Bitmask of free scalar registers
│   │   ├── vec_free_mask (uint32_t): Bitmask of free vector registers
│   │   ├── id_to_reg[]: Virtual → Physical scalar mapping
│   │   ├── id_to_vec_reg[]: Virtual → Physical vector mapping
│   │   ├── reg_to_id[]: Physical → Virtual scalar reverse mapping
│   │   └── vec_reg_to_id[]: Physical → Virtual vector reverse mapping
│   ├── Main Execution
│   │   └── execute(stream, liveness)
│   │       ├── Transaction Check: begin_strike() if needed
│   │       ├── State Reset: Clear all mappings and masks
│   │       ├── Linear Scan Loop
│   │       │   ├── expire_dead_registers(): Reclaim expired registers
│   │       │   ├── Input Resolution: Map virtual operands to physical
│   │       │   ├── Allocation: Assign physical register for result
│   │       │   └── Sentinel Refresh: Update intent_hash if changed
│   ├── Register Allocation
│   │   ├── find_and_occupy_reg(): Hardware-accelerated bit scan
│   │   │   ├── std::countr_zero() for free register finding
│   │   │   ├── Update free_mask and mappings
│   │   │   └── Return physical register index
│   │   └── find_and_occupy_vec_reg(): Vector register allocation
│   ├── Register Resolution
│   │   ├── get_phys_reg(): Virtual → Physical scalar lookup
│   │   └── get_phys_vec_reg(): Virtual → Physical vector lookup
│   └── Register Reclamation
│       └── expire_dead_registers()
│           ├── Scalar Reclamation: Check liveness, free expired
│           └── Vector Reclamation: Same for NEON registers
└── Integration Points
    ├── IRStream: Transactional shadow buffer access
    ├── LivenessAnalyzer: is_live_at() for expiration checks
    └── IRNode: calculate_hash() for sentinel validation
```

**Data Flow:**
1. **Initialization**: Allocate mapping vectors, initialize bitmasks to all-free state
2. **Execution Start**: Ensure transaction active, reset all state for clean strike
3. **Linear Scan**: Process each instruction in order:
   - Expire registers no longer live at current position
   - Resolve virtual operands to physical registers
   - Allocate physical register for result (if needed)
   - Update sentinel hash if any mappings changed
4. **Allocation**: Use hardware bit scan to find free registers, update all mappings
5. **Expiration**: Query liveness analyzer to reclaim registers whose values are dead

## Summary

ARM64_RegAlloc.hpp implements a high-performance linear scan register allocator specifically optimized for DGTL's transactional IR system and ARM64 architecture. The allocator operates during the "strike" phase of compilation, mapping virtual result_ids to physical ARM64 registers while maintaining Pillar 3 sentinel validation. It uses hardware-accelerated bit operations for O(N) allocation performance and integrates deeply with the liveness analysis and transactional shadow buffers.

Key features include:
- **Hardware-Accelerated Allocation**: Uses std::countr_zero (TZCNT/CLZ) for instant free register finding
- **Transactional Safety**: Operates on shadow buffers with automatic sentinel hash updates
- **Dual Register Classes**: Separate allocation for scalar GPRs and NEON vector registers
- **Liveness-Aware Expiration**: Reclaims registers based on precise liveness information
- **Spill Candidate Marking**: Flags instructions that couldn't get registers
- **ARM64-Specific Constraints**: Respects reserved registers (FP, LR, SP, ZR)

The implementation achieves zero-overhead register allocation with compile-time constant register counts and bitmask operations.

## Full Explanation of Code

### Namespace and Includes
```cpp
#pragma once
#include <array>
#include <cstdint>
#include <vector>
#include <bit>
#include <algorithm>
#include "../ir/Global_IR.hpp"
#include "../analysis/Liveness_Analyzer.hpp"

namespace dgtl::opt {
```
- **#pragma once**: Header guard
- **Standard Includes**:
  - `<array>`: Fixed-size arrays for reverse mappings
  - `<cstdint>`: Fixed-width integers
  - `<vector>`: Dynamic arrays for virtual→physical mappings
  - `<bit>`: C++20 bit operations (countr_zero)
  - `<algorithm>`: For std::fill
- **Internal Includes**:
  - `"../ir/Global_IR.hpp"`: IR definitions, OpCode, Flags
  - `"../analysis/Liveness_Analyzer.hpp"`: Liveness queries
- **Namespace**: dgtl::opt (optimization namespace)

### ARM64Regs Constants
```cpp
struct ARM64Regs {
    static constexpr uint32_t TotalGPRs = 31;
    static constexpr uint32_t X_ZR = 31;         // Zero Register
    static constexpr uint32_t X_SP = 31;         // Stack Pointer
    static constexpr uint32_t Reserved_FP = 29;  // Frame Pointer (X29)
    static constexpr uint32_t Reserved_LR = 30;  // Link Register (X30)
    
    // Mask for allocatable registers (X0-X28). 1 = Available.
    static constexpr uint32_t ALLOCABLE_MASK = 0x1FFFFFFF; 

    // NEON Vector Registers (V0-V31)
    static constexpr uint32_t TotalVecRegs = 32;
    static constexpr uint32_t VEC_ALLOCABLE_MASK = 0xFFFFFFFF;
};
```
**Register Layout**:
- **X0-X30**: 31 general-purpose registers
- **Reserved**: X29 (FP), X30 (LR), X31 (ZR/SP)
- **Allocatable**: X0-X28 (29 registers, mask 0x1FFFFFFF)
- **Vectors**: V0-V31 (32 registers, all allocatable)

### DGTLARM64RegAlloc Class Members
```cpp
class DGTLARM64RegAlloc {
    // 32-bit bitset: 1 = Free, 0 = Occupied.
    uint32_t free_mask = ARM64Regs::ALLOCABLE_MASK;
    
    // NEON bitset: 1 = Free, 0 = Occupied.
    uint32_t vec_free_mask = ARM64Regs::VEC_ALLOCABLE_MASK;

    // Mapping: Virtual ResultID -> Physical Register Index (0-30). 0xFF = Not mapped.
    std::vector<uint8_t> id_to_reg;
    
    // Mapping: Virtual ResultID -> Physical Vector Register Index (0-31).
    std::vector<uint8_t> id_to_vec_reg;
    
    // Reverse Mapping: Physical Register Index -> ResultID currently held.
    std::array<uint32_t, 32> reg_to_id;
    
    // Reverse Mapping: Physical Vector Register -> ResultID currently held.
    std::array<uint32_t, 32> vec_reg_to_id;
```
**State Tracking**:
- **free_mask**: Bitmask where 1 = free scalar register
- **vec_free_mask**: Bitmask where 1 = free vector register
- **id_to_reg**: Virtual result_id → physical register (0xFF = unmapped)
- **id_to_vec_reg**: Virtual result_id → physical vector register
- **reg_to_id**: Physical register → virtual result_id (reverse lookup)
- **vec_reg_to_id**: Physical vector register → virtual result_id

### Constructor
```cpp
explicit DGTLARM64RegAlloc(size_t max_results = 1048576) { // Increased for 1M node strikes
    id_to_reg.resize(max_results, 0xFF);
    id_to_vec_reg.resize(max_results, 0xFF);
    reg_to_id.fill(0);
    vec_reg_to_id.fill(0);
}
```
**Purpose**: Initialize allocator for large compilation units
**Sizing**: 1M maximum result IDs (increased from typical 64K)
**Initialization**: All mappings cleared (0xFF/0), ready for allocation

### Main Execution Method

#### execute()
```cpp
void execute(ir::IRStream& stream, const dgtl::core::LivenessAnalyzer& liveness) {
    // [DGTL CRITICAL] Must be in a transaction to modify shadow buffers
    if (!stream.is_active_transaction()) {
        stream.begin_strike();
    }

    // Reset state for the current transactional strike
    free_mask = ARM64Regs::ALLOCABLE_MASK;
    vec_free_mask = ARM64Regs::VEC_ALLOCABLE_MASK;
    reg_to_id.fill(0);
    vec_reg_to_id.fill(0);
    std::fill(id_to_reg.begin(), id_to_reg.end(), 0xFF);
    std::fill(id_to_vec_reg.begin(), id_to_vec_reg.end(), 0xFF);

    const size_t count = stream.size();
    auto* atoms = stream.raw_atoms(); // Returns shadow_atoms if in transaction
    auto* cores = stream.raw_cores(); // Returns shadow_cores if in transaction

    for (uint32_t i = 0; i < count; ++i) {
        // 1. EXPIRATION: Reclaim registers whose liveness ends before this instruction.
        expire_dead_registers(i, liveness);

        // 2. INPUT RESOLUTION: Map virtual operands to physical registers.
        bool changed = false;
        
        // Check if this is a SIMD operation to use the correct mapping table
        const bool is_simd = (atoms[i].flags & ir::Flags::IS_SIMD_BURST);

        if (cores[i].op_a != ir::INVALID_SLOT) {
            uint8_t phys = is_simd ? get_phys_vec_reg(cores[i].op_a) : get_phys_reg(cores[i].op_a);
            if (phys != 0xFF) { cores[i].op_a = phys; changed = true; }
        }
        if (cores[i].op_b != ir::INVALID_SLOT) {
            uint8_t phys = is_simd ? get_phys_vec_reg(cores[i].op_b) : get_phys_reg(cores[i].op_b);
            if (phys != 0xFF) { cores[i].op_b = phys; changed = true; }
        }

        // 3. ALLOCATION: Assign physical destination for the result_id.
        if (cores[i].result_id != 0 && atoms[i].opcode != ir::OpCode::NOP) {
            uint32_t phys_dest = is_simd ? find_and_occupy_vec_reg(cores[i].result_id) 
                                         : find_and_occupy_reg(cores[i].result_id);
            
            if (phys_dest != 0xFF) [[likely]] {
                // Update op_c as the physical destination register
                cores[i].op_c = phys_dest; 
                atoms[i].flags |= ir::Flags::SVE_ACTIVE; // Mark as Hardware-Mapped
                changed = true;
            } else {
                atoms[i].flags |= ir::Flags::IS_SPILL_CANDIDATE; 
            }
        }

        // 4. [PILLAR 3] REFRESH SENTINEL HASH
        // If we changed operands to physical registers, the hash MUST be updated
        // or the validate_shadow_integrity() call will fail during commit.
        if (changed) {
            // FIX: Use the standardized IRNode hashing to prevent transaction abortion
            cores[i].intent_hash = ir::IRNode::calculate_hash(
                atoms[i].opcode, 
                cores[i].op_a, 
                cores[i].op_b, 
                cores[i].op_c
            );
        }
    }
}
```
**Algorithm**: Linear scan register allocation with liveness-guided expiration
**Phases**:
1. **Expiration**: Reclaim registers no longer live
2. **Input Resolution**: Map virtual operands to physical registers
3. **Allocation**: Assign physical register for result
4. **Sentinel Update**: Refresh intent hash if mappings changed

**Transactional Safety**: Operates on shadow buffers, updates sentinels

### Register Allocation Methods

#### find_and_occupy_reg()
```cpp
[[nodiscard]] uint32_t find_and_occupy_reg(uint32_t result_id) noexcept {
    if (free_mask == 0) return 0xFF; 

    // std::countr_zero maps to TZCNT (x64) or CLZ (ARM)
    uint32_t reg = static_cast<uint32_t>(std::countr_zero(free_mask));
    
    free_mask &= ~(1u << reg); 
    reg_to_id[reg] = result_id;
    
    if (result_id < id_to_reg.size()) [[likely]] {
        id_to_reg[result_id] = static_cast<uint8_t>(reg);
    }
    
    return reg;
}
```
**Algorithm**: Hardware-accelerated free register finding
- **Bit Scan**: std::countr_zero finds first set bit (free register)
- **Occupation**: Clear bit in free_mask, update mappings
- **Return**: Physical register index or 0xFF if none available

#### find_and_occupy_vec_reg()
```cpp
[[nodiscard]] uint32_t find_and_occupy_vec_reg(uint32_t result_id) noexcept {
    if (vec_free_mask == 0) return 0xFF;
    uint32_t reg = static_cast<uint32_t>(std::countr_zero(vec_free_mask));
    vec_free_mask &= ~(1u << reg);
    vec_reg_to_id[reg] = result_id;
    if (result_id < id_to_vec_reg.size()) id_to_vec_reg[result_id] = static_cast<uint8_t>(reg);
    return reg;
}
```
**Purpose**: Vector register allocation (NEON/SVE)
**Logic**: Identical to scalar version but for vector registers

### Register Resolution Methods

#### get_phys_reg() / get_phys_vec_reg()
```cpp
[[nodiscard]] uint8_t get_phys_reg(uint32_t virtual_id) const noexcept {
    if (virtual_id >= id_to_reg.size()) return 0xFF;
    return id_to_reg[virtual_id];
}

[[nodiscard]] uint8_t get_phys_vec_reg(uint32_t virtual_id) const noexcept {
    if (virtual_id >= id_to_vec_reg.size()) return 0xFF;
    return id_to_vec_reg[virtual_id];
}
```
**Purpose**: Lookup physical register for virtual result_id
**Bounds Check**: Return 0xFF for out-of-bounds virtual IDs

### Register Expiration

#### expire_dead_registers()
```cpp
void expire_dead_registers(uint32_t current_idx, const dgtl::core::LivenessAnalyzer& liveness) noexcept {
    // Scalar Reclamation
    uint32_t occupied = ~free_mask & ARM64Regs::ALLOCABLE_MASK;
    while (occupied) {
        uint32_t reg = static_cast<uint32_t>(std::countr_zero(occupied));
        uint32_t vid = reg_to_id[reg];
        if (!liveness.is_live_at(vid, current_idx)) {
            free_mask |= (1u << reg);
            if (vid < id_to_reg.size()) id_to_reg[vid] = 0xFF;
            reg_to_id[reg] = 0;
        }
        occupied &= ~(1u << reg);
    }

    // Vector Reclamation (NEON)
    uint32_t v_occupied = ~vec_free_mask & ARM64Regs::VEC_ALLOCABLE_MASK;
    while (v_occupied) {
        uint32_t reg = static_cast<uint32_t>(std::countr_zero(v_occupied));
        uint32_t vid = vec_reg_to_id[reg];
        if (!liveness.is_live_at(vid, current_idx)) {
            vec_free_mask |= (1u << reg);
            if (vid < id_to_vec_reg.size()) id_to_vec_reg[vid] = 0xFF;
            vec_reg_to_id[reg] = 0;
        }
        v_occupied &= ~(1u << reg);
    }
}
```
**Algorithm**: Scan all occupied registers, free those no longer live
**Liveness Query**: is_live_at(vid, current_idx) determines if value survives past current instruction
**Cleanup**: Reset mappings when freeing registers

### Namespace Closing
```cpp
} // namespace dgtl::opt
```
- Closes the optimization namespace

## Usage Example
```cpp
#include "ARM64_RegAlloc.hpp"

// Create allocator for large functions
dgtl::opt::DGTLARM64RegAlloc allocator(100000); // 100K max result IDs

// During compilation strike
dgtl::ir::IRStream stream(/*...*/);
dgtl::core::LivenessAnalyzer liveness(/*...*/);

// Execute register allocation
allocator.execute(stream, liveness);

// After allocation, operands contain physical register numbers
// result_id registers are assigned to op_c field
// Spill candidates are marked with IS_SPILL_CANDIDATE flag
```

## Dependencies
- **Global_IR.hpp**: OpCode, Flags, INVALID_SLOT definitions
- **Liveness_Analyzer.hpp**: is_live_at() method for expiration decisions
- **IRStream**: Transactional shadow buffer access
- **C++20**: std::countr_zero for hardware-accelerated bit scanning

## Thread Safety
- **Instance Methods**: Not thread-safe, single allocator per compilation context
- **Read-Only Operations**: get_phys_reg() family are const and thread-safe
- **Execution**: execute() requires exclusive access to IRStream

## Performance Characteristics
- **Time Complexity**: O(N) where N is instruction count
- **Space Complexity**: O(M) where M is max result IDs (1M default)
- **Register Finding**: O(1) hardware-accelerated with std::countr_zero
- **Memory Access**: Contiguous arrays for cache efficiency
- **Bit Operations**: 32-bit masks for fast register state queries
- **Liveness Queries**: External liveness analyzer provides O(1) lookups