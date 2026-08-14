/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository: github.com/TataG-Systems/DGTL
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

module;

#include <cstdlib>

#include <array>
#include <cstdint>
#include <cstddef>
#include <vector>
#include <bit>
#include <algorithm>
#include <limits>

export module dgtl.ir_core.arm64_regalloc;

import dgtl.ir_core;
import dgtl.ir_core.runtime_contract;
import dgtl.ir_core.bridge;

export namespace dgtl::isa {

struct ARM64Regs {
    static constexpr uint32_t TotalGPRs = 31;
    [[maybe_unused]] static constexpr uint32_t X_ZR = 31;         // Zero Register
    [[maybe_unused]] static constexpr uint32_t X_SP = 31;         // Stack Pointer
    [[maybe_unused]] static constexpr uint32_t Reserved_FP = 29;  // Frame Pointer (X29)
    [[maybe_unused]] static constexpr uint32_t Reserved_LR = 30;  // Link Register (X30)
    
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

    struct OperandUseMask {
        bool use_a = false;
        bool use_b = false;
        bool use_c = false;
    };

    [[nodiscard]] static OperandUseMask operand_uses(ir::OpCode opcode) noexcept {
        switch (opcode) {
            case ir::OpCode::LOAD_IMM:
            case ir::OpCode::IMMEDIATE:
            case ir::OpCode::STRING_CONST:
            case ir::OpCode::LABEL:
            case ir::OpCode::NOP:
                return {false, false, false};

            case ir::OpCode::RET:
            case ir::OpCode::REG_MOVE:
            case ir::OpCode::LOAD:
                return {true, false, false};

            case ir::OpCode::STORE:
                return {true, true, false};

            case ir::OpCode::BRANCH_COND:
            case ir::OpCode::BRANCH_LIKELY:
            case ir::OpCode::BRANCH_UNLIKELY:
                return {true, false, false};

            case ir::OpCode::BRANCH:
            case ir::OpCode::GOTO:
                return {false, false, false};

            case ir::OpCode::FMA_F32:
            case ir::OpCode::CALL:
            case ir::OpCode::CALL_VARIADIC:
                return {true, true, true};

            default:
                if (dgtl::isa::lowering::runtime_contract::has_runtime_symbol(opcode)) {
                    return {true, true, true};
                }
                return {true, true, false};
        }
    }

public:
    static constexpr uint8_t UNMAPPED_REG = 0xFF;

    explicit DGTLARM64RegAlloc(size_t max_results = 1048576) { // Increased for 1M node strikes
        id_to_reg.resize(max_results, UNMAPPED_REG);
        id_to_vec_reg.resize(max_results, UNMAPPED_REG);
        reg_to_id.fill(ir::INVALID_SLOT);
        vec_reg_to_id.fill(ir::INVALID_SLOT);
    }

    /**
     * @brief Linear Scan Allocation Strike.
     * Operates on the Shadow Buffer during the DGTLoptManager strike phase.
     */
    void execute(ir::IRStream& stream, const dgtl::analysis::LivenessAnalyzer& liveness) {
        if (!dgtl::ir::bridge_validation::is_stream_ready(stream)) {
            return;
        }

        // [DGTL CRITICAL] Must be in a transaction to modify shadow buffers
        bool owns_transaction = false;
        if (!stream.is_active_transaction()) {
            stream.begin_transaction();
            owns_transaction = true;
        }

        // Reset state for the current transactional strike
        free_mask = ARM64Regs::ALLOCABLE_MASK;
        vec_free_mask = ARM64Regs::VEC_ALLOCABLE_MASK;
        reg_to_id.fill(ir::INVALID_SLOT);
        vec_reg_to_id.fill(ir::INVALID_SLOT);
        std::fill(id_to_reg.begin(), id_to_reg.end(), UNMAPPED_REG);
        std::fill(id_to_vec_reg.begin(), id_to_vec_reg.end(), UNMAPPED_REG);

        const size_t count = stream.size();
        auto* atoms = stream.raw_atoms(); // Returns shadow_atoms if in transaction
        auto* cores = stream.raw_cores(); // Returns shadow_cores if in transaction
        if (!atoms || !cores) {
            if (owns_transaction) {
                stream.rollback_transaction();
            }
            return;
        }

        const size_t max_walk = std::min(count, static_cast<size_t>((std::numeric_limits<uint32_t>::max)()));

        for (size_t idx = 0; idx < max_walk; ++idx) {
            const uint32_t i = static_cast<uint32_t>(idx);
            // 1. EXPIRATION: Reclaim registers whose liveness ends before this instruction.
            expire_dead_registers(i, liveness);

            // 2. INPUT RESOLUTION: Map virtual operands to physical registers.
            bool changed = false;
            uint32_t preferred_reg = 0xFF; // Preference for coalescing
            
            // Check if this is a SIMD operation to use the correct mapping table
            const bool is_simd = (atoms[i].flags & ir::Flags::IS_SIMD_BURST);
            const bool is_imm_node =
                atoms[i].opcode == ir::OpCode::LOAD_IMM ||
                atoms[i].opcode == ir::OpCode::IMMEDIATE ||
                atoms[i].opcode == ir::OpCode::CONST;
            const OperandUseMask uses = operand_uses(atoms[i].opcode);

            if (cores[i].result_id != ir::INVALID_SLOT) {
                ensure_capacity(cores[i].result_id);
            }
            if (cores[i].op_a != ir::INVALID_SLOT) {
                ensure_capacity(cores[i].op_a);
            }
            if (cores[i].op_b != ir::INVALID_SLOT) {
                ensure_capacity(cores[i].op_b);
            }

            if (!is_imm_node && uses.use_a && cores[i].op_a != ir::INVALID_SLOT) {
                uint8_t phys = is_simd ? get_phys_vec_reg(cores[i].op_a) : get_phys_reg(cores[i].op_a);
                if (phys == UNMAPPED_REG) {
                    phys = is_simd ? map_live_in_vec_reg(cores[i].op_a) : map_live_in_reg(cores[i].op_a);
                }
                if (phys != UNMAPPED_REG) {
                    cores[i].op_a = phys; 
                    changed = true; 
                    // COALESCING: If op_a dies here, prefer its register for the result
                    const uint32_t src_vid = is_simd ? vec_reg_to_id[phys] : reg_to_id[phys];
                    if (src_vid != ir::INVALID_SLOT && !liveness.is_live_after(src_vid, i)) {
                        preferred_reg = phys;
                    }
                }
            }
            if (!is_imm_node && uses.use_b && cores[i].op_b != ir::INVALID_SLOT) {
                uint8_t phys = is_simd ? get_phys_vec_reg(cores[i].op_b) : get_phys_reg(cores[i].op_b);
                if (phys == UNMAPPED_REG) {
                    phys = is_simd ? map_live_in_vec_reg(cores[i].op_b) : map_live_in_reg(cores[i].op_b);
                }
                if (phys != UNMAPPED_REG) {
                    cores[i].op_b = phys; 
                    changed = true; 
                    // COALESCING: Secondary preference if op_b dies
                    if (preferred_reg == UNMAPPED_REG) {
                        const uint32_t src_vid = is_simd ? vec_reg_to_id[phys] : reg_to_id[phys];
                        if (src_vid != ir::INVALID_SLOT && !liveness.is_live_after(src_vid, i)) {
                            preferred_reg = phys;
                        }
                    }
                }
            }

            if (!is_imm_node && uses.use_c && cores[i].op_c != ir::INVALID_SLOT) {
                uint8_t phys = is_simd ? get_phys_vec_reg(cores[i].op_c) : get_phys_reg(cores[i].op_c);
                if (phys == UNMAPPED_REG) {
                    phys = is_simd ? map_live_in_vec_reg(cores[i].op_c) : map_live_in_reg(cores[i].op_c);
                }
                if (phys != UNMAPPED_REG) {
                    cores[i].op_c = phys;
                    changed = true;
                }
            }

            // 3. ALLOCATION: Assign physical destination for the result_id.
            if (cores[i].result_id != ir::INVALID_SLOT && atoms[i].opcode != ir::OpCode::NOP) {
                uint32_t phys_dest = UNMAPPED_REG;

                // Try to use preferred coalesced register to eliminate MOV
                if (preferred_reg != UNMAPPED_REG) {
                    phys_dest = is_simd ? occupy_vec_reg_specific(cores[i].result_id, preferred_reg)
                                        : occupy_reg_specific(cores[i].result_id, preferred_reg);
                }

                // If coalescing failed, the preferred register is still occupied by the dying
                // operand (expire_dead_registers runs at the TOP of the loop, before operand
                // resolution, so the old mapping was never cleared). Release it now so the
                // result can inherit the slot without consuming a fresh register.
                if (phys_dest == UNMAPPED_REG && preferred_reg != UNMAPPED_REG) {
                    auto release_dying = [&](uint32_t reg, bool simd) {
                        uint32_t old_vid = simd ? vec_reg_to_id[reg] : reg_to_id[reg];
                        if (old_vid == ir::INVALID_SLOT) return;
                        if (liveness.is_live_after(old_vid, i)) return; // still needed
                        if (simd) {
                            vec_free_mask |= (1u << reg);
                            if (old_vid < id_to_vec_reg.size()) id_to_vec_reg[old_vid] = UNMAPPED_REG;
                            vec_reg_to_id[reg] = ir::INVALID_SLOT;
                        } else {
                            free_mask |= (1u << reg);
                            if (old_vid < id_to_reg.size()) id_to_reg[old_vid] = UNMAPPED_REG;
                            reg_to_id[reg] = ir::INVALID_SLOT;
                        }
                    };
                    release_dying(preferred_reg, is_simd);
                    phys_dest = is_simd ? occupy_vec_reg_specific(cores[i].result_id, preferred_reg)
                                       : occupy_reg_specific(cores[i].result_id, preferred_reg);
                }

                // Fallback to hardware-accelerated scan if coalescing fails
                if (phys_dest == UNMAPPED_REG) {
                    phys_dest = is_simd ? find_and_occupy_vec_reg(cores[i].result_id) 
                                         : find_and_occupy_reg(cores[i].result_id);
                }
                
                if (phys_dest != UNMAPPED_REG) [[likely]] {
                    // Update op_c as the physical destination register
                    cores[i].op_c = phys_dest; 
                    cores[i].logic_flags |= ir::HintFlags::SVEActive; // Mark as Hardware-Mapped
                    changed = true;
                } else {
                    cores[i].logic_flags |= ir::HintFlags::IsSpillCandidate; 
                }
            }

            // 4. [PILLAR 3] REFRESH SENTINEL HASH
            if (changed) {
                cores[i].intent_hash = ir::IRNode::calculate_hash(
                    atoms[i].opcode, 
                    cores[i].result_id,
                    cores[i].op_a, 
                    cores[i].op_b, 
                    cores[i].op_c
                );
            }
        }

        if (owns_transaction) {
            stream.commit_transaction();
        }
    }

private:
    void ensure_capacity(uint32_t virtual_id) {
        if (virtual_id == ir::INVALID_SLOT) {
            return;
        }
        const size_t needed = static_cast<size_t>(virtual_id) + 1u;
        if (needed <= id_to_reg.size()) {
            return;
        }
        id_to_reg.resize(needed, UNMAPPED_REG);
        id_to_vec_reg.resize(needed, UNMAPPED_REG);
    }

    [[nodiscard]] uint8_t map_live_in_reg(uint32_t virtual_id) noexcept {
        const uint32_t phys = find_and_occupy_reg(virtual_id);
        if (phys == UNMAPPED_REG) {
            return UNMAPPED_REG;
        }
        return static_cast<uint8_t>(phys);
    }

    [[nodiscard]] uint8_t map_live_in_vec_reg(uint32_t virtual_id) noexcept {
        const uint32_t phys = find_and_occupy_vec_reg(virtual_id);
        if (phys == UNMAPPED_REG) {
            return UNMAPPED_REG;
        }
        return static_cast<uint8_t>(phys);
    }

    /**
     * @brief Specific occupancy for register coalescing.
     */
    [[nodiscard]] uint32_t occupy_reg_specific(uint32_t result_id, uint32_t reg) noexcept {
        if (reg >= ARM64Regs::TotalGPRs) return UNMAPPED_REG;
        if (!(free_mask & (1u << reg))) return UNMAPPED_REG;
        free_mask &= ~(1u << reg);
        reg_to_id[reg] = result_id;
        if (result_id < id_to_reg.size()) [[likely]] id_to_reg[result_id] = static_cast<uint8_t>(reg);
        return reg;
    }

    [[nodiscard]] uint32_t occupy_vec_reg_specific(uint32_t result_id, uint32_t reg) noexcept {
        if (reg >= ARM64Regs::TotalVecRegs) return UNMAPPED_REG;
        if (!(vec_free_mask & (1u << reg))) return UNMAPPED_REG;
        vec_free_mask &= ~(1u << reg);
        vec_reg_to_id[reg] = result_id;
        if (result_id < id_to_vec_reg.size()) id_to_vec_reg[result_id] = static_cast<uint8_t>(reg);
        return reg;
    }

    /**
     * @brief Hardware-accelerated bit-scanning (CTZ) to find free registers.
     */
    [[nodiscard]] uint32_t find_and_occupy_reg(uint32_t result_id) noexcept {
        if (free_mask == 0) return UNMAPPED_REG;
        uint32_t reg = static_cast<uint32_t>(std::countr_zero(free_mask));
        free_mask &= ~(1u << reg); 
        reg_to_id[reg] = result_id;
        if (result_id < id_to_reg.size()) [[likely]] id_to_reg[result_id] = static_cast<uint8_t>(reg);
        return reg;
    }

    [[nodiscard]] uint32_t find_and_occupy_vec_reg(uint32_t result_id) noexcept {
        if (vec_free_mask == 0) return UNMAPPED_REG;
        uint32_t reg = static_cast<uint32_t>(std::countr_zero(vec_free_mask));
        vec_free_mask &= ~(1u << reg);
        vec_reg_to_id[reg] = result_id;
        if (result_id < id_to_vec_reg.size()) id_to_vec_reg[result_id] = static_cast<uint8_t>(reg);
        return reg;
    }

    [[nodiscard]] uint8_t get_phys_reg(uint32_t virtual_id) const noexcept {
        if (virtual_id >= id_to_reg.size()) return UNMAPPED_REG;
        return id_to_reg[virtual_id];
    }

    [[nodiscard]] uint8_t get_phys_vec_reg(uint32_t virtual_id) const noexcept {
        if (virtual_id >= id_to_vec_reg.size()) return UNMAPPED_REG;
        return id_to_vec_reg[virtual_id];
    }

    /**
     * @brief Frees registers based on Liveness Data.
     * PERFORMANCE: Integrated bitmask clear to avoid unnecessary while-loop iterations.
     */
    void expire_dead_registers(uint32_t current_idx, const dgtl::analysis::LivenessAnalyzer& liveness) noexcept {
        uint32_t occupied = ~free_mask & ARM64Regs::ALLOCABLE_MASK;
        while (occupied) {
            uint32_t reg = static_cast<uint32_t>(std::countr_zero(occupied));
            uint32_t vid = reg_to_id[reg];
            if (vid != ir::INVALID_SLOT && !liveness.is_live_at(vid, current_idx)) {
                free_mask |= (1u << reg);
                if (vid < id_to_reg.size()) id_to_reg[vid] = UNMAPPED_REG;
                reg_to_id[reg] = ir::INVALID_SLOT;
            }
            occupied &= ~(1u << reg);
        }

        uint32_t v_occupied = ~vec_free_mask & ARM64Regs::VEC_ALLOCABLE_MASK;
        while (v_occupied) {
            uint32_t reg = static_cast<uint32_t>(std::countr_zero(v_occupied));
            uint32_t vid = vec_reg_to_id[reg];
            if (vid != ir::INVALID_SLOT && !liveness.is_live_at(vid, current_idx)) {
                vec_free_mask |= (1u << reg);
                if (vid < id_to_vec_reg.size()) id_to_vec_reg[vid] = UNMAPPED_REG;
                vec_reg_to_id[reg] = ir::INVALID_SLOT;
            }
            v_occupied &= ~(1u << reg);
        }
    }
};

} // namespace dgtl::isa