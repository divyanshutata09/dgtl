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

#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "dgtl/ir/base/IR_Enums.hpp"

namespace dgtl::ir {

struct IRCore {
    uint32_t result_id = 0;

    uint32_t op_a = 0;
    uint32_t op_b = 0;
    uint32_t op_c = 0;

    uint64_t intent_hash = 0;

    uint32_t logic_flags = 0;
    uint32_t aux_flags   = 0;

    uint32_t debug_id    = 0;
    uint32_t epoch       = 0;
};

#pragma pack(push, 2)
struct IRAtom {
    OpCode opcode;
    uint16_t flags;

    constexpr IRAtom() = default;
    constexpr IRAtom(OpCode op, uint16_t fl) noexcept : opcode(op), flags(fl) {}

    constexpr operator uint32_t() const noexcept {
        return (static_cast<uint32_t>(opcode) << 16) | flags;
    }

    constexpr IRAtom& operator=(uint32_t value) noexcept {
        opcode = static_cast<OpCode>(value >> 16);
        flags = static_cast<uint16_t>(value & 0xFFFF);
        return *this;
    }
};
#pragma pack(pop)

struct AtomSplitter {

    static constexpr OpCode get_opcode(IRAtom atom) noexcept {
        return atom.opcode;
    }

    static constexpr uint16_t get_flags(IRAtom atom) noexcept {
        return atom.flags;
    }

    static constexpr IRAtom pack(OpCode op, uint16_t flags) noexcept {
        return IRAtom(op, flags);
    }

    static constexpr IRAtom pack(uint32_t value) noexcept {
        return IRAtom(
            static_cast<OpCode>(value >> 16),
            static_cast<uint16_t>(value & 0xFFFF)
        );
    }
};

namespace detail {

[[nodiscard]] static constexpr bool irnode_is_branch(OpCode op) noexcept {
    return op == OpCode::BRANCH ||
           op == OpCode::BRANCH_COND ||
           op == OpCode::BRANCH_COMPARE_FUSED ||
           op == OpCode::GOTO;
}

[[nodiscard]] static constexpr bool irnode_is_simd(OpCode op) noexcept {
    return op == OpCode::GATHER_F32_512 ||
           op == OpCode::GATHER_16 ||
           op == OpCode::STORE_V512 ||
           op == OpCode::VEC_ADD_F32_512 ||
           op == OpCode::VEC_MUL_F32_512 ||
           op == OpCode::VEC_LOAD ||
           op == OpCode::VEC_STORE ||
           op == OpCode::VEC_SHUFFLE ||
           op == OpCode::VEC_REDUCE_ADD ||
           op == OpCode::FCVTL_F16_TO_F32 ||
           op == OpCode::FCVTN_F32_TO_F16 ||
           op == OpCode::SIMD_BSL_PREDICATE;
}

[[nodiscard]] static constexpr bool irnode_has_side_effects(OpCode op, uint16_t flags) noexcept {
    if (flags & Flags::HAS_SIDE_EFFECTS) return true;
    if (flags & Flags::IS_VOLATILE)       return true;

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

[[nodiscard]] static constexpr double irnode_inline_weight(OpCode op) noexcept {
    if (op == OpCode::GATHER_F32_512 || op == OpCode::GATHER_16) return 50.0;
    if (irnode_is_branch(op)) return 5.0;
    if (op == OpCode::SYSCALL) return 100.0;
    if (irnode_is_simd(op)) return 3.0;
    return 1.0;
}

} // namespace detail

#pragma pack(push, 1)

struct alignas(32) IRNode {
    OpCode   opcode;
    uint16_t flags;

    uint32_t result_id;
    uint32_t op_a;
    uint32_t op_b;
    uint32_t op_c;
    uint64_t intent_hash;
    uint32_t logic_flags;

    [[nodiscard]] constexpr bool is_terminator() const noexcept {
        if (flags & Flags::IS_RETURN) return true;
        return detail::irnode_is_branch(opcode) || opcode == OpCode::RET || opcode == OpCode::RETURN;
    }

    [[nodiscard]] inline bool is_volatile() const noexcept {
        return (flags & Flags::IS_VOLATILE) != 0;
    }

    [[nodiscard]] inline bool has_side_effects() const noexcept {
        return detail::irnode_has_side_effects(opcode, flags);
    }

    [[nodiscard]] inline bool is_valid() const noexcept {
        return opcode != OpCode::NOP;
    }

    [[nodiscard]] inline uint64_t logic_hash() const noexcept {
        return intent_hash ^ (static_cast<uint64_t>(result_id) << 32);
    }

    [[nodiscard]] inline double predict_inlined_benefit() const noexcept {
        return detail::irnode_inline_weight(opcode);
    }

    [[nodiscard]] inline IRNode clone() const noexcept { return *this; }

    [[nodiscard]] static constexpr uint64_t calculate_hash(OpCode op,
                                                           uint32_t res,
                                                           uint32_t a,
                                                           uint32_t b,
                                                           uint32_t c) noexcept {
        uint64_t h = static_cast<uint64_t>(op) ^ 0x9e3779b97f4a7c15ULL;
        auto mix = [&](uint32_t val) {
            h ^= (static_cast<uint64_t>(val) + 0x517cc1b727220a95ULL + (h << 6) + (h >> 2));
        };
        mix(res); mix(a); mix(b); mix(c);
        return h;
    }

    [[nodiscard]] static constexpr uint64_t calculate_semantic_hash(
        OpCode op, uint32_t a, uint32_t b, uint32_t c,
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

    inline void refresh_intent_hash() noexcept {
        intent_hash = calculate_hash(opcode, result_id, op_a, op_b, op_c);
    }

    inline void fold_if_possible() noexcept {
        if (!(flags & Flags::IS_SPEC_CONST)) return;

        bool folded = true;
        uint64_t result = 0;

        switch (opcode) {
            case OpCode::ADD_I32: result = static_cast<uint64_t>(static_cast<int32_t>(op_a) + static_cast<int32_t>(op_b)); break;
            case OpCode::SUB_I32: result = static_cast<uint64_t>(static_cast<int32_t>(op_a) - static_cast<int32_t>(op_b)); break;
            case OpCode::MUL_I32: result = static_cast<uint64_t>(static_cast<int32_t>(op_a) * static_cast<int32_t>(op_b)); break;
            case OpCode::DIV_I32:
                if (static_cast<int32_t>(op_b) == 0) { folded = false; }
                else { result = static_cast<uint64_t>(static_cast<int32_t>(op_a) / static_cast<int32_t>(op_b)); }
                break;
            case OpCode::AND_I32: result = static_cast<uint64_t>(op_a & op_b); break;
            case OpCode::OR_I32:  result = static_cast<uint64_t>(op_a | op_b); break;
            case OpCode::XOR_I32: result = static_cast<uint64_t>(op_a ^ op_b); break;
            case OpCode::SHL_I32: result = static_cast<uint64_t>(static_cast<int32_t>(op_a) << (op_b & 31U)); break;
            case OpCode::SHR_I32: result = static_cast<uint64_t>(static_cast<int32_t>(op_a) >> (op_b & 31U)); break;
            case OpCode::MOD_I32:
                if (static_cast<int32_t>(op_b) == 0) { folded = false; }
                else { result = static_cast<uint64_t>(static_cast<int32_t>(op_a) % static_cast<int32_t>(op_b)); }
                break;
            case OpCode::ADD_I64:
            case OpCode::SUB_I64:
            case OpCode::MUL_I64:
            case OpCode::DIV_I64:
            case OpCode::BIT_FIELD_INSERT:
            case OpCode::BIT_FIELD_EXTRACT:
                folded = false;
                break;
            case OpCode::ADD_F32: {
                float a = std::bit_cast<float>(op_a), b = std::bit_cast<float>(op_b);
                result = std::bit_cast<uint32_t>(a + b);
                break;
            }
            case OpCode::SUB_F32: {
                float a = std::bit_cast<float>(op_a), b = std::bit_cast<float>(op_b);
                result = std::bit_cast<uint32_t>(a - b);
                break;
            }
            case OpCode::MUL_F32: {
                float a = std::bit_cast<float>(op_a), b = std::bit_cast<float>(op_b);
                result = std::bit_cast<uint32_t>(a * b);
                break;
            }
            case OpCode::DIV_F32: {
                float a = std::bit_cast<float>(op_a), b = std::bit_cast<float>(op_b);
                if (b == 0.0f) { folded = false; }
                else { result = std::bit_cast<uint32_t>(a / b); }
                break;
            }
            case OpCode::FMA_F32: {
                float a = std::bit_cast<float>(op_a);
                float b = std::bit_cast<float>(op_b);
                float c = std::bit_cast<float>(op_c);
                result = std::bit_cast<uint32_t>(a * b + c);
                break;
            }
            case OpCode::ADD_F64: case OpCode::SUB_F64:
            case OpCode::MUL_F64: case OpCode::DIV_F64:
                folded = false;
                break;
            default:
                folded = false;
                break;
        }

        if (!folded) return;

        opcode = OpCode::IMMEDIATE;
        op_a = 0;
        op_b = static_cast<uint32_t>(result & 0xFFFFFFFF);
        op_c = 0;
        flags |= Flags::IS_CONST;
        refresh_intent_hash();
    }
};

#pragma pack(pop)

static_assert(sizeof(IRNode) == 32,      "IRNode must be exactly 32 bytes for AVX-512 alignment - BIR 1.0.0 backend parity contract");
static_assert(alignof(IRNode) == 32,     "IRNode must have 32-byte alignment for cache-line and AVX-512 compatibility");
static_assert(std::is_trivially_copyable_v<IRNode>,  "IRNode must be trivially copyable for transactional streaming across all backends");
static_assert(std::is_standard_layout_v<IRNode>,     "IRNode must be standard-layout for deterministic cross-platform ABI (x64/ARM64/WASM)");
static_assert(offsetof(IRNode, opcode)      ==  0,   "IRNode::opcode must be at offset 0 - BIR 1.0.0 backend parity");
static_assert(offsetof(IRNode, flags)       ==  2,   "IRNode::flags must be at offset 2 - BIR 1.0.0 backend parity");
static_assert(offsetof(IRNode, result_id)   ==  4,   "IRNode::result_id must be at offset 4 - BIR 1.0.0 backend parity");
static_assert(offsetof(IRNode, op_a)        ==  8,   "IRNode::op_a must be at offset 8 - BIR 1.0.0 backend parity");
static_assert(offsetof(IRNode, op_b)        == 12,   "IRNode::op_b must be at offset 12 - BIR 1.0.0 backend parity");
static_assert(offsetof(IRNode, op_c)        == 16,   "IRNode::op_c must be at offset 16 - BIR 1.0.0 backend parity");
static_assert(offsetof(IRNode, intent_hash) == 20,   "IRNode::intent_hash must be at offset 20 - BIR 1.0.0 backend parity");
static_assert(offsetof(IRNode, logic_flags) == 28,   "IRNode::logic_flags must be at offset 28 - BIR 1.0.0 backend parity");
static_assert(sizeof(float)  == 4, "float must be 32-bit IEEE 754 on all backends");
static_assert(sizeof(double) == 8, "double must be 64-bit IEEE 754 on all backends");
static_assert(sizeof(int)    == 4, "int must be 32-bit on all DGTL-supported platforms");

static_assert(sizeof(IRAtom)  == 4,  "IRAtom must be exactly 4 bytes - SoA stream width contract");
static_assert(alignof(IRAtom) == 2,  "IRAtom must be 2-byte aligned for packed stream access");
static_assert(std::is_trivially_copyable_v<IRAtom>, "IRAtom must be trivially copyable");

static_assert(sizeof(IRCore)  == 40, "IRCore must be exactly 40 bytes - tier-2 SoA stride contract");
static_assert(std::is_trivially_copyable_v<IRCore>, "IRCore must be trivially copyable for shadow-buffer streaming");
static_assert(std::is_standard_layout_v<IRCore>,    "IRCore must be standard-layout for cross-backend ABI parity");

} // namespace dgtl::ir
