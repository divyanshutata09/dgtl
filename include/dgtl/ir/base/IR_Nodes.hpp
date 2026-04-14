/* * DGTL-MLIR-X v1.0 : THE COMPILER SPEC
 * STEP 2: BIT-PACKED INTERMEDIATE REPRESENTATION (BIR) - VERSION 2.1 (RETURN FIX)
 * PILLAR 2: TRI-TIER TRANSACTIONAL STREAM & SHADOW-AWARE EMISSION
 * UNIFIED IR NODE ARCHITECTURE (Tier 1 + Tier 2 fused view)
 * STANDARDS: C++26 / UCRT64 (Zero-Overhead Primitive Mapping)
 */

#pragma once

#include "IR_Types.hpp"
#include "IR_Traits.hpp"
#include <type_traits>

namespace dgtl::ir {

#pragma pack(push, 1)

/**
 * @struct IRNode
 * @brief Unified view for high-level manipulation.
 * Maps Tier 1 (Atom) and Tier 2 (Core) into a 32-byte cache-aligned structure.
 */
struct alignas(32) IRNode {
    // --- Header (Tier 1 Mirror: 4 bytes) ---
    OpCode   opcode;    // 2 bytes
    uint16_t flags;     // 2 bytes

    // --- Payload (Tier 2 Mirror: 28 bytes) ---
    uint32_t result_id;   // 4 bytes
    uint32_t op_a;        // 4 bytes
    uint32_t op_b;        // 4 bytes
    uint32_t op_c;        // 4 bytes
    uint64_t intent_hash; // 8 bytes
    uint32_t logic_flags; // 4 bytes

    // --- Semantic Queries ---

    [[nodiscard]] constexpr bool is_terminator() const noexcept {
        // BIR 2.1 Logic: Explicit check + Trait fallback
        if (opcode == OpCode::RET || opcode == OpCode::GOTO || 
            opcode == OpCode::BRANCH || (flags & Flags::IS_RETURN)) {
            return true;
        }
        return IRTraits::is_terminator(opcode);
    }

    [[nodiscard]] inline bool is_volatile() const noexcept {
        return (flags & Flags::IS_VOLATILE) != 0;
    }

    [[nodiscard]] inline bool has_side_effects() const noexcept {
        return IRTraits::has_side_effects(opcode, flags);
    }

    [[nodiscard]] inline bool is_valid() const noexcept {
        return opcode != OpCode::NOP;
    }

    [[nodiscard]] inline uint64_t logic_hash() const noexcept {
        return intent_hash ^ (static_cast<uint64_t>(result_id) << 32);
    }

    [[nodiscard]] inline double predict_inlined_benefit() const noexcept {
        return IRTraits::get_inline_weight(opcode);
    }

    [[nodiscard]] inline IRNode clone() const noexcept { return *this; }

    // --- Static Utilities ---

    static constexpr uint64_t calculate_hash(OpCode op, uint32_t res, uint32_t a, uint32_t b, uint32_t c) noexcept {
        uint64_t h = static_cast<uint64_t>(op) ^ 0x9e3779b97f4a7c15ULL;
        auto mix = [&](uint32_t val) {
            h ^= (static_cast<uint64_t>(val) + 0x517cc1b727220a95ULL + (h << 6) + (h >> 2));
        };
        mix(res); mix(a); mix(b); mix(c);
        return h;
    }

    // Semantic hash deliberately excludes result_id so CSE/GVN can match
    // equivalent expressions produced into different virtual registers.
    static constexpr uint64_t calculate_semantic_hash(
        OpCode op,
        uint32_t a,
        uint32_t b,
        uint32_t c,
        uint16_t flags = Flags::NONE) noexcept {
        uint64_t h = static_cast<uint64_t>(op) ^ 0xD6E8FEB86659FD93ULL;
        auto mix = [&](uint64_t val) {
            h ^= val + 0x9E3779B97F4A7C15ULL + (h << 6) + (h >> 2);
        };
        mix(static_cast<uint64_t>(flags));
        mix(static_cast<uint64_t>(a));
        mix(static_cast<uint64_t>(b));
        mix(static_cast<uint64_t>(c));
        return h;
    }

    // --- Mutators ---

    inline void refresh_intent_hash() noexcept {
        intent_hash = calculate_hash(opcode, result_id, op_a, op_b, op_c);
    }

    inline void fold_if_possible() noexcept {
        if (!(flags & Flags::IS_SPEC_CONST)) return;

        bool folded = true;
        uint32_t result = 0;

        switch (opcode) {
            case OpCode::ADD_I32: result = op_a + op_b; break;
            case OpCode::SUB_I32: result = op_a - op_b; break;
            case OpCode::MUL_I32: result = op_a * op_b; break;
            case OpCode::DIV_I32:
                if (op_b == 0) {
                    folded = false;
                } else {
                    result = op_a / op_b;
                }
                break;
            case OpCode::AND_I32: result = op_a & op_b; break;
            case OpCode::OR_I32:  result = op_a | op_b; break;
            case OpCode::XOR_I32: result = op_a ^ op_b; break;
            case OpCode::SHL_I32: result = op_a << (op_b & 31U); break;
            default:
                folded = false;
                break;
        }

        if (!folded) return;

        opcode = OpCode::IMMEDIATE;
        op_a = 0;
        op_b = result;
        op_c = 0;
        flags |= Flags::IS_CONST;
        refresh_intent_hash();
    }
};

#pragma pack(pop)

// Integrity Checks
static_assert(sizeof(IRNode) == 32, "IRNode must be exactly 32 bytes for AVX-512 alignment");
static_assert(std::is_trivially_copyable_v<IRNode>, "IRNode must be POD for transactional streaming");

} // namespace dgtl::ir