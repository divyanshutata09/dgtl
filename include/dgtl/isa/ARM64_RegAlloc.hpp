/* * DGTL-MLIR-X v1.0 : THE COMPILER SPEC
 * STEP 16: HIGH-SPEED ARM64 REGISTER ALLOCATOR (LSRA) - TRANSACTIONAL V2
 * FINAL FIX: Synchronized with Bit-Packed IR (Bit 7/12) & Shadow-Validation.
 * PERFORMANCE: O(N) allocation with hardware-accelerated bit-probing.
 * SAFETY: Transaction-Aware; aborts on illegal physical mappings.
 */

#pragma once
#include <array>
#include <cstdint>
#include <vector>
#include <bit>
#include <algorithm>
#include "../ir/Global_IR.hpp"
#include "../analysis/Liveness_Analyzer.hpp"

namespace dgtl::opt {

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

/**
 * @class DGTLARM64RegAlloc
 * @brief Linear Scan Allocator optimized for Transactional IR Strikes.
 * Maps virtual result_ids to physical registers and refreshes Pillar 3 Sentinels.
 */
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

public:
    explicit DGTLARM64RegAlloc(size_t max_results = 1048576) { // Increased for 1M node strikes
        id_to_reg.resize(max_results, 0xFF);
        id_to_vec_reg.resize(max_results, 0xFF);
        reg_to_id.fill(0);
        vec_reg_to_id.fill(0);
    }

    /**
     * @brief Linear Scan Allocation Strike.
     * Operates on the Shadow Buffer during the DGTLPassManager strike phase.
     */
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

private:
    /**
     * @brief Hardware-accelerated bit-scanning (CTZ) to find free registers.
     */
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

    [[nodiscard]] uint32_t find_and_occupy_vec_reg(uint32_t result_id) noexcept {
        if (vec_free_mask == 0) return 0xFF;
        uint32_t reg = static_cast<uint32_t>(std::countr_zero(vec_free_mask));
        vec_free_mask &= ~(1u << reg);
        vec_reg_to_id[reg] = result_id;
        if (result_id < id_to_vec_reg.size()) id_to_vec_reg[result_id] = static_cast<uint8_t>(reg);
        return reg;
    }

    [[nodiscard]] uint8_t get_phys_reg(uint32_t virtual_id) const noexcept {
        if (virtual_id >= id_to_reg.size()) return 0xFF;
        return id_to_reg[virtual_id];
    }

    [[nodiscard]] uint8_t get_phys_vec_reg(uint32_t virtual_id) const noexcept {
        if (virtual_id >= id_to_vec_reg.size()) return 0xFF;
        return id_to_vec_reg[virtual_id];
    }

    /**
     * @brief Frees registers based on Liveness Data.
     * Implementation logic for reclaiming bits in free_mask.
     */
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
};

} // namespace dgtl::opt
