/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:    DGTL (Deep General-purpose Transformation Layer)
 * Owner:      Divyanshu Tata
 * Repository: github.com/TataG-Systems/DGTL
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

module;

#include <cstdlib>

// Global Module Fragment: Capture core memory layout requirements safely
#include <bit>
#include <cstddef>
#include <cstdint>
#include <type_traits>

export module dgtl.ir_node;

// Dependency Ordered Named Module Imports
export import dgtl.ir_enums;
import dgtl.ir_schema;
import dgtl.ir.gen;

// HIR representation only: target lowering/MIR state must not be stored here.
// Semantic classification is derived exclusively from canonical schema metadata.

export namespace dgtl::ir {

struct IRCore {
    std::uint32_t result_id = 0u;

    std::uint32_t op_a = 0u;
    std::uint32_t op_b = 0u;
    std::uint32_t op_c = 0u;

    std::uint64_t intent_hash = 0ull;

    std::uint32_t logic_flags = 0u;
    std::uint32_t aux_flags   = 0u;

    std::uint32_t debug_id    = 0u;
    std::uint32_t epoch       = 0u;
};

#pragma pack(push, 2)
struct IRAtom {
    OpCode opcode = OpCode::NOP;
    std::uint16_t flags = 0;

    constexpr IRAtom() = default;
    constexpr IRAtom(OpCode op, std::uint16_t fl) noexcept : opcode(op), flags(fl) {}

    [[nodiscard]] constexpr operator std::uint32_t() const noexcept {
        return (static_cast<std::uint32_t>(opcode) << 16) | flags;
    }

    constexpr IRAtom& operator=(std::uint32_t value) noexcept {
        opcode = static_cast<OpCode>(value >> 16);
        flags = static_cast<std::uint16_t>(value & 0xFFFFu);
        return *this;
    }
};
#pragma pack(pop)

struct AtomSplitter {
    [[nodiscard]] static constexpr OpCode get_opcode(IRAtom atom) noexcept {
        return atom.opcode;
    }

    [[nodiscard]] static constexpr std::uint16_t get_flags(IRAtom atom) noexcept {
        return atom.flags;
    }

    [[nodiscard]] static constexpr IRAtom pack(OpCode op, std::uint16_t flags) noexcept {
        return IRAtom(op, flags);
    }

    [[nodiscard]] static constexpr IRAtom pack(std::uint32_t value) noexcept {
        return IRAtom(
            static_cast<OpCode>(value >> 16),
            static_cast<std::uint16_t>(value & 0xFFFFu)
        );
    }
};

namespace detail {

[[nodiscard]] inline constexpr bool irnode_is_branch(OpCode op) noexcept {
    return gen::op_has_trait(op, gen::GenTrait::IsTerminator) &&
           gen::op_implements_interface(op, gen::GenInterface::ControlFlow);
}

[[nodiscard]] inline constexpr bool irnode_has_side_effects(
    OpCode op, std::uint16_t flags) noexcept {
    if ((flags & Flags::HAS_SIDE_EFFECTS) != 0u) return true;
    if ((flags & Flags::IS_VOLATILE) != 0u) return true;
    if (!schema::is_canonical(op)) return true;
    return !gen::op_has_trait(op, gen::GenTrait::HasNoSideEffects);
}

[[nodiscard]] inline constexpr double irnode_inline_weight(OpCode op) noexcept {
    if (irnode_is_branch(op)) return 5.0;
    if (gen::op_implements_interface(op, gen::GenInterface::SIMD) ||
        gen::op_has_trait(op, gen::GenTrait::IsVectorizable)) return 3.0;
    return 1.0;
}

} // namespace detail

#pragma pack(push, 1)

struct alignas(32) IRNode {
    OpCode        opcode = OpCode::NOP;
    std::uint16_t flags = 0;

    std::uint32_t result_id = 0;
    std::uint32_t op_a = 0;
    std::uint32_t op_b = 0;
    std::uint32_t op_c = 0;
    std::uint64_t intent_hash = 0;
    std::uint32_t logic_flags = 0;

    [[nodiscard]] constexpr bool is_terminator() const noexcept {
        return (flags & Flags::IS_RETURN) != 0u ||
               gen::op_has_trait(opcode, gen::GenTrait::IsTerminator);
    }

    [[nodiscard]] constexpr bool is_volatile() const noexcept {
        return (flags & Flags::IS_VOLATILE) != 0u;
    }

    [[nodiscard]] constexpr bool has_side_effects() const noexcept {
        return detail::irnode_has_side_effects(opcode, flags);
    }

    [[nodiscard]] constexpr bool is_valid() const noexcept {
        return opcode != OpCode::NOP;
    }

    [[nodiscard]] constexpr std::uint64_t logic_hash() const noexcept {
        return intent_hash ^ (static_cast<std::uint64_t>(result_id) << 32);
    }

    [[nodiscard]] constexpr double predict_inlined_benefit() const noexcept {
        return detail::irnode_inline_weight(opcode);
    }

    [[nodiscard]] constexpr bool is_memory_read() const noexcept {
        return gen::op_has_trait(opcode, gen::GenTrait::IsMemoryRead);
    }

    [[nodiscard]] constexpr bool is_memory_write() const noexcept {
        return gen::op_has_trait(opcode, gen::GenTrait::IsMemoryWrite);
    }

    [[nodiscard]] constexpr bool is_constant_foldable() const noexcept {
        return gen::op_has_trait(opcode, gen::GenTrait::ConstantFoldable);
    }

    [[nodiscard]] constexpr bool has_result() const noexcept {
        if (const auto* def = gen::find_generated_op_def(opcode)) return def->has_result;
        return false;
    }

    [[nodiscard]] constexpr std::uint16_t stable_opcode_id() const noexcept {
        return schema::stable_id(opcode);
    }

    [[nodiscard]] constexpr IRNode clone() const noexcept { return *this; }

    [[nodiscard]] static constexpr std::uint64_t calculate_hash(OpCode op,
                                                                 std::uint32_t res,
                                                                 std::uint32_t a,
                                                                 std::uint32_t b,
                                                                 std::uint32_t c) noexcept {
        std::uint64_t h = static_cast<std::uint64_t>(op) ^ 0x9e3779b97f4a7c15ULL;
        auto mix = [&](std::uint32_t val) {
            h ^= (static_cast<std::uint64_t>(val) + 0x517cc1b727220a95ULL + (h << 6) + (h >> 2));
        };
        mix(res); mix(a); mix(b); mix(c);
        return h;
    }

    [[nodiscard]] static constexpr std::uint64_t calculate_semantic_hash(
        OpCode op, std::uint32_t a, std::uint32_t b, std::uint32_t c,
        std::uint16_t flags = Flags::NONE) noexcept {
        std::uint64_t h = static_cast<std::uint64_t>(op) ^ 0xD6E8FEB86659FD93ULL;
        auto mix = [&](std::uint64_t val) {
            h ^= val + 0x9E3779B97F4A7C15ULL + (h << 6) + (h >> 2);
        };
        mix(static_cast<std::uint64_t>(flags));
        mix(static_cast<std::uint64_t>(a));
        mix(static_cast<std::uint64_t>(b));
        mix(static_cast<std::uint64_t>(c));
        return h;
    }

    constexpr void refresh_intent_hash() noexcept {
        intent_hash = calculate_hash(opcode, result_id, op_a, op_b, op_c);
    }

    // Constant folding is intentionally implemented by HIR transformation passes.
    // IRNode is a passive representation and never owns rewrite semantics.


};

#pragma pack(pop)

static_assert(sizeof(IRNode) == 32,      "IRNode must be exactly 32 bytes for cache-line aligned HIR storage contract");
static_assert(alignof(IRNode) == 32,     "IRNode must have 32-byte alignment for cache-line-aligned canonical HIR storage");
static_assert(std::is_trivially_copyable_v<IRNode>,  "IRNode must be trivially copyable for transactional HIR storage");
static_assert(std::is_standard_layout_v<IRNode>,     "IRNode must be standard-layout for deterministic HIR object layout");
static_assert(offsetof(IRNode, opcode)      ==  0,   "IRNode::opcode must be at offset 0 - stable HIR layout");
static_assert(offsetof(IRNode, flags)       ==  2,   "IRNode::flags must be at offset 2 - stable HIR layout");
static_assert(offsetof(IRNode, result_id)   ==  4,   "IRNode::result_id must be at offset 4 - stable HIR layout");
static_assert(offsetof(IRNode, op_a)        ==  8,   "IRNode::op_a must be at offset 8 - stable HIR layout");
static_assert(offsetof(IRNode, op_b)        == 12,   "IRNode::op_b must be at offset 12 - stable HIR layout");
static_assert(offsetof(IRNode, op_c)        == 16,   "IRNode::op_c must be at offset 16 - stable HIR layout");
static_assert(offsetof(IRNode, intent_hash) == 20,   "IRNode::intent_hash must be at offset 20 - stable HIR layout");
static_assert(offsetof(IRNode, logic_flags) == 28,   "IRNode::logic_flags must be at offset 28 - stable HIR layout");
static_assert(sizeof(float)  == 4, "float must be 32-bit IEEE 754 on conforming IEEE-754 C++ implementations");
static_assert(sizeof(double) == 8, "double must be 64-bit IEEE 754 on conforming IEEE-754 C++ implementations");
static_assert(sizeof(int)    == 4, "int must be 32-bit on all DGTL-supported platforms");

static_assert(sizeof(IRAtom)  == 4,  "IRAtom must be exactly 4 bytes - SoA stream width contract");
static_assert(alignof(IRAtom) == 2,  "IRAtom must be 2-byte aligned for packed stream access");
static_assert(std::is_trivially_copyable_v<IRAtom>, "IRAtom must be trivially copyable");

static_assert(sizeof(IRCore)  == 40, "IRCore must be exactly 40 bytes - tier-2 SoA stride contract");
static_assert(std::is_trivially_copyable_v<IRCore>, "IRCore must be trivially copyable for shadow-buffer streaming");
static_assert(std::is_standard_layout_v<IRCore>,    "IRCore must be standard-layout for stable HIR layout");

} // namespace dgtl::ir




