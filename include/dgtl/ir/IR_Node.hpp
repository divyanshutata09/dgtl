/* * DGTL-MLIR-X v1.0 : THE COMPILER SPEC
 * STEP 2: BIT-PACKED INTERMEDIATE REPRESENTATION (BIR) - VERSION 2.1
 * PILLAR 2: TRI-TIER TRANSACTIONAL STREAM & SHADOW-AWARE EMISSION
 * PHYSICAL NODE LAYOUT & UNIFIED VIEW
 * STANDARDS: C++26 / UCRT64 (Zero-Overhead Unified Mapping)
 */

#pragma once
#include <cstdint>
#include <algorithm>
#include "IR_Types.hpp"

namespace dgtl::ir {

#pragma pack(push, 1)

/**
 * @struct IRNode
 * @brief Unified view of an instruction for high-level manipulation.
 * Maps Tier 1 (Atom) and Tier 2 (Core) into a single 24-byte structure.
 * Contains the logic for terminators, side-effects, and intent-hashing.
 */
struct IRNode {
    // --- Header (Tier 1 Mirror) ---
    OpCode   opcode;    // 2 bytes
    uint16_t flags;     // 2 bytes

    // --- Payload (Tier 2 Mirror) ---
    uint32_t result_id; // 4 bytes
    uint32_t op_a;      // 4 bytes
    uint32_t op_b;      // 4 bytes
    uint32_t op_c;      // 4 bytes
    uint32_t intent_hash; // 4 bytes

    // --- Semantic Queries ---

    [[nodiscard]] inline bool is_terminator() const noexcept {
        return opcode == OpCode::BRANCH || 
               opcode == OpCode::RET || 
               opcode == OpCode::RETURN || 
               opcode == OpCode::BRANCH_COND ||
               opcode == OpCode::GOTO;
    }

    [[nodiscard]] inline bool is_volatile() const noexcept { 
        return (flags & Flags::IS_VOLATILE) != 0; 
    }
    
    [[nodiscard]] inline bool has_side_effects() const noexcept { 
        // Sync with BIR 2.1: Includes explicit HAS_SIDE_EFFECTS and IS_RETURN flags
        return (flags & Flags::HAS_SIDE_EFFECTS) != 0 || 
               (flags & Flags::IS_RETURN) != 0 ||
               opcode == OpCode::STORE || 
               opcode == OpCode::STORE_V512 ||
               opcode == OpCode::CALL ||
               opcode == OpCode::SYSCALL ||
               opcode == OpCode::RET ||
               opcode == OpCode::RETURN; 
    }
    
    [[nodiscard]] inline bool is_valid() const noexcept { 
        return opcode != OpCode::NOP; 
    }
    
    /**
     * @brief Logic Hash for Pillar 3 validation.
     * Combines intent (operation) with result (identity) for strict collision checks.
     */
    [[nodiscard]] inline uint64_t logic_hash() const noexcept {
        return (static_cast<uint64_t>(intent_hash) << 32) | result_id;
    }

    [[nodiscard]] inline double predict_inlined_benefit() const noexcept {
        if (opcode == OpCode::GATHER_F32_512 || opcode == OpCode::GATHER_16) return 50.0;
        if (opcode == OpCode::SYSCALL) return 100.0; 
        return 1.0; 
    }

    [[nodiscard]] inline IRNode clone() const noexcept { return *this; }

    /**
     * @brief Murmur-inspired mixing for low-collision intent hashing.
     * Optimized for UCRT64/AVX registers.
     */
    static constexpr uint32_t calculate_hash(OpCode op, uint32_t a, uint32_t b, uint32_t c) noexcept {
        uint32_t h = static_cast<uint32_t>(static_cast<uint16_t>(op));
        auto mix = [&](uint32_t val) {
            h ^= val + 0x9e3779b9 + (h << 6) + (h >> 2);
        };
        mix(a);
        mix(b);
        mix(c);
        return h;
    }

    inline void refresh_intent_hash() noexcept {
        intent_hash = calculate_hash(opcode, op_a, op_b, op_c);
    }

    /**
     * @brief Zero-latency constant folding strike.
     */
    inline void fold_if_possible() noexcept {
        if (!(flags & Flags::IS_SPEC_CONST)) return;
        if (opcode == OpCode::ADD_I32 || opcode == OpCode::MUL_I32) {
            opcode = OpCode::IMMEDIATE;
            refresh_intent_hash();
        }
    }
};

#pragma pack(pop)

// --- Invariants for HFT/Sub-millisecond execution tiers ---
// IRAtom and IRCore are defined in IR_Types.hpp to maintain SoA consistency.
static_assert(sizeof(IRNode) == 24, "IRNode unified view must be exactly 24 bytes for HFT alignment");

} // namespace dgtl::ir

/**
 * @brief Backward Compatibility Alias for Legacy DGTL Logic.
 */
namespace dgtl::ir {
    using namespace dgtl::ir;
}
