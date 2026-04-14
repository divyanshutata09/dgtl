/* * DGTL-MLIR-X v1.0 : THE COMPILER SPEC
 * STEP 2: BIT-PACKED INTERMEDIATE REPRESENTATION (BIR) - VERSION 2.1 (RETURN FIX)
 * PILLAR 2: TRI-TIER TRANSACTIONAL STREAM & SHADOW-AWARE EMISSION
 * FUNDAMENTAL traits and semantic queries for IR nodes and opcodes.
 * STANDARDS: C++26 / UCRT64 (Zero-Overhead Primitive Mapping)
 */

#pragma once
#include <cstdint>
#include <bit>
#include <type_traits>

// Ensure IR_Types defines the OpCode and Flags used below
#include "IR_Types.hpp"

/**
 * @section Preprocessor Hygiene
 * Neutralize Windows macro pollution to prevent "expected unqualified-id" errors.
 */
#ifdef CONST
    #undef CONST
#endif
#ifdef MOV
    #undef MOV
#endif

namespace dgtl::ir {


/**
 * @class IRTraits
 * @brief Static utility traits for rapid semantic analysis of OpCodes and Flags.
 * Guaranteed zero-overhead as all functions are constexpr/inline.
 */
struct IRTraits {
    
    [[nodiscard]] static constexpr bool is_branch(OpCode op) noexcept {
        return op == OpCode::BRANCH || 
               op == OpCode::BRANCH_COND || 
               op == OpCode::BRANCH_COMPARE_FUSED || 
               op == OpCode::GOTO;
    }

    [[nodiscard]] static constexpr bool is_terminator(OpCode op) noexcept {
        return is_branch(op) || op == OpCode::RET || op == OpCode::RETURN;
    }

    [[nodiscard]] static constexpr bool is_memory_op(OpCode op) noexcept {
        return op == OpCode::LOAD || 
               op == OpCode::STORE || 
               op == OpCode::GATHER_F32_512 || 
               op == OpCode::GATHER_16 ||
               op == OpCode::STORE_V512 ||
               op == OpCode::LEA;
    }

    [[nodiscard]] static constexpr bool has_side_effects(OpCode op, uint16_t flags) noexcept {
        // Direct flag checks
        if (flags & Flags::HAS_SIDE_EFFECTS) return true; 
        if (flags & Flags::IS_VOLATILE)       return true; 
        
        // OpCode-level architecture invariants
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

    [[nodiscard]] static constexpr bool is_simd(OpCode op) noexcept {
        return op == OpCode::GATHER_F32_512 || 
               op == OpCode::GATHER_16 ||
               op == OpCode::STORE_V512 ||
               op == OpCode::VEC_ADD_F32_512 ||
               op == OpCode::VEC_MUL_F32_512;
    }

    [[nodiscard]] static constexpr bool is_valid_slot(uint32_t slot_id) noexcept {
        return slot_id != 0xFFFFFFFF && slot_id < 0xFFFFFF00;
    }

    [[nodiscard]] static constexpr double get_inline_weight(OpCode op) noexcept {
        if (op == OpCode::GATHER_F32_512 || op == OpCode::GATHER_16) return 50.0;
        if (is_branch(op)) return 5.0;
        if (op == OpCode::SYSCALL) return 100.0; 
        return 1.0;
    }
};

#include "IR_Nodes.hpp"

} // namespace dgtl::ir