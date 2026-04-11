/* * DGTL-MLIR-X v1.0 : THE COMPILER SPEC
 * STEP 2: BIT-PACKED INTERMEDIATE REPRESENTATION (BIR) - VERSION 2.1
 * PILLAR 2: TRI-TIER TRANSACTIONAL STREAM & SHADOW-AWARE EMISSION
 * TRAITS ENGINE: Semantic Queries & Utility Invariants
 * STANDARDS: C++26 / UCRT64 (Zero-Overhead Invariants)
 */

#pragma once
#include "IR_Types.hpp"

namespace dgtl::ir {

/**
 * @class IRTraits
 * @brief Static utility traits for rapid semantic analysis of OpCodes and Flags.
 * Guaranteed zero-overhead as all functions are constexpr/inline.
 */
struct IRTraits {
    
    /**
     * @brief Checks if the OpCode represents a control-flow jump.
     */
    [[nodiscard]] static constexpr bool is_branch(OpCode op) noexcept {
        return op == OpCode::BRANCH || 
               op == OpCode::BRANCH_COND || 
               op == OpCode::BRANCH_COMPARE_FUSED || 
               op == OpCode::GOTO;
    }

    /**
     * @brief Determines if the OpCode ends the current basic block.
     * ✅ BIR 2.1: Includes RET as a primary terminator.
     */
    [[nodiscard]] static constexpr bool is_terminator(OpCode op) noexcept {
        return is_branch(op) || op == OpCode::RET || op == OpCode::RETURN;
    }

    /**
     * @brief Identifies operations interacting with the data cache or memory.
     */
    [[nodiscard]] static constexpr bool is_memory_op(OpCode op) noexcept {
        return op == OpCode::LOAD || 
               op == OpCode::STORE || 
               op == OpCode::GATHER_F32_512 || 
               op == OpCode::GATHER_16 ||
               op == OpCode::STORE_V512 ||
               op == OpCode::LEA;
    }

    /**
     * @brief Pillar 3 side-effect detection.
     * Combines explicit bit-flags with OpCode-level architecture invariants.
     * @note Crucial for GVN (Global Value Numbering) safety. If this returns true,
     * the instruction must NOT be deduplicated or deleted.
     */
    [[nodiscard]] static constexpr bool has_side_effects(OpCode op, uint16_t flags) noexcept {
        // Direct flag checks: Bitmask 0x0200 covers HAS_SIDE_EFFECTS
        if (flags & Flags::HAS_SIDE_EFFECTS) return true; 
        if (flags & Flags::IS_VOLATILE)       return true; 
        if (flags & Flags::IS_RETURN)         return true;
        
        // OpCode-level safety anchors:
        // Explicit switch for strict compliance with Logic Tests 11, 12, 14, and 20.
        switch (op) {
            case OpCode::STORE:
            case OpCode::STORE_V512:
            case OpCode::CALL:
            case OpCode::SYSCALL:
            case OpCode::RET:
            case OpCode::BRANCH:
            case OpCode::BRANCH_COND:
            case OpCode::GOTO:
            case OpCode::GUARD_ALIAS:
            case OpCode::GUARD_VALUE:
                return true;
            default:
                return false;
        }
    }

    /**
     * @brief Checks for AVX-512 / SIMD instructions.
     */
    [[nodiscard]] static constexpr bool is_simd(OpCode op) noexcept {
        return op == OpCode::GATHER_F32_512 || 
               op == OpCode::GATHER_16 ||
               op == OpCode::STORE_V512 ||
               op == OpCode::VEC_ADD_F32_512 ||
               op == OpCode::VEC_MUL_F32_512;
    }

    /**
     * @brief [PILLAR 3] Invariant validation for Slot IDs.
     * Ensures IDs are within the valid 24-bit to 32-bit addressable range.
     */
    [[nodiscard]] static constexpr bool is_valid_slot(uint32_t slot_id) noexcept {
        return slot_id != INVALID_SLOT && slot_id < 0xFFFFFF00;
    }

    /**
     * @brief Heuristic for inlining benefit during optimization passes.
     */
    [[nodiscard]] static constexpr double get_inline_weight(OpCode op) noexcept {
        if (op == OpCode::GATHER_F32_512 || op == OpCode::GATHER_16) return 50.0;
        if (is_branch(op)) return 5.0;
        if (op == OpCode::SYSCALL) return 100.0; 
        return 1.0;
    }
};

/**
 * @section Global Assertions
 * Validating SoA (Structure of Arrays) memory layout for high-performance emission.
 */
static_assert(sizeof(IRAtom) == 4, "IRAtom must be 4 bytes for cache-line packing");
static_assert(sizeof(IRCore) == 20, "IRCore must be 20 bytes to satisfy Sentinel intent_hash alignment");

} // namespace dgtl::ir

/**
 * @brief Backward Compatibility Alias for Legacy DGTL Logic.
 */
namespace dgtl::ir {
    using namespace dgtl::ir;
}
