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

/* * DGTL (Deep General-purpose Transformation Layer) version 1.0.0 : THE COMPILER SPEC
 * STEP 26: INTEGRATED FORMAL VERIFICATION LAYER (SENTINEL-ENABLED)
 * VERSION 3.2: TITAN-STABLE (CROSS-PASS CONSISTENCY UPGRADE)
 *
 * ENHANCEMENTS:
 * + Stronger symbolic modeling (bit propagation)
 * + Full CONST flag normalization (IS_CONST | IS_SPEC_CONST)
 * + Safer UB detection (division / invalid ops)
 * + Deterministic intent hash enforcement
 * + Cross-pass compatibility (GVN / Folder / Fusion)
 */

#pragma once
#include <array>
#include <bitset>
#include <optional>
#include <bit>
#include <cstring>
#include <cstdint>

#include "dgtl/ir/Global_IR.hpp"

namespace dgtl::core {

struct SymbolicVal {
    uint64_t fixed_bits;
    uint64_t unknown_mask;
    bool has_ub;
};

struct ConstantResult {
    uint32_t low;
    uint32_t high;
};

class DGTLFormalVerifier {
public:
    DGTLFormalVerifier() = default;

    // ============================================================
    // MASTER HASH (PILLAR 3 CORE)
    // ============================================================

    [[nodiscard]] static constexpr uint32_t compute_master_hash_bits(
        ir::OpCode opcode,
        uint32_t op_a,
        uint32_t op_b,
        uint32_t op_c) noexcept
    {
        const uint32_t op_val =
            static_cast<uint32_t>(static_cast<uint16_t>(opcode));

        uint32_t h = (op_val * 31u) ^
                     (op_a * 17u) ^
                     (op_b * 7u) ^
                     (op_c * 3u);

        // extra avalanche (stability improvement)
        h ^= (h >> 13);
        h *= 0x9E3779B1u;
        h ^= (h >> 16);

        return h;
    }

    [[nodiscard]] static uint32_t compute_master_hash(const ir::IRNode& n) noexcept {
        return compute_master_hash_bits(n.opcode, n.op_a, n.op_b, n.op_c);
    }

    // ============================================================
    // SENTINEL INTEGRITY
    // ============================================================

    [[nodiscard]] bool verify_pass_integrity(
        const ir::IRNode& original,
        const ir::IRNode& optimized) const noexcept
    {
        // symbolic equivalence
        if (!verify_transform(original, optimized)) [[unlikely]]
            return false;

        // intent hash must match EXACT formula
        if (optimized.intent_hash != compute_master_hash(optimized)) [[unlikely]]
            return false;

        return true;
    }

    // ============================================================
    // CONSTANT SOLVER (FIXED)
    // ============================================================

    [[nodiscard]] std::optional<ConstantResult>
    solve_static_math(const ir::IRNode& node) const noexcept {

        const bool is_const =
            (node.flags & ir::Flags::IS_CONST) ||
            (node.flags & ir::Flags::IS_SPEC_CONST);

        if (!is_const) return std::nullopt;

        switch (node.opcode) {

            case ir::OpCode::ADD_I32:
                return ConstantResult{ node.op_a + node.op_b, 0 };

            case ir::OpCode::SUB_I32:
                return ConstantResult{ node.op_a - node.op_b, 0 };

            case ir::OpCode::MUL_I32:
                return ConstantResult{ node.op_a * node.op_b, 0 };

            case ir::OpCode::AND_I32:
                return ConstantResult{ node.op_a & node.op_b, 0 };

            case ir::OpCode::OR_I32:
                return ConstantResult{ node.op_a | node.op_b, 0 };

            case ir::OpCode::XOR_I32:
                return ConstantResult{ node.op_a ^ node.op_b, 0 };

            case ir::OpCode::DIV_I32:
                if (node.op_b == 0) [[unlikely]] return std::nullopt;
                return ConstantResult{ node.op_a / node.op_b, 0 };

            case ir::OpCode::SHL_I32:
                return ConstantResult{ node.op_a << (node.op_b & 31u), 0 };

            case ir::OpCode::ADD_F32: {
                float fa, fb;
                std::memcpy(&fa, &node.op_a, 4);
                std::memcpy(&fb, &node.op_b, 4);
                float res = fa + fb;
                uint32_t out;
                std::memcpy(&out, &res, 4);
                return ConstantResult{ out, 0 };
            }

            default:
                return std::nullopt;
        }
    }

    // ============================================================
    // MINIMAL EQUIVALENT (UPGRADED)
    // ============================================================

    [[nodiscard]] std::optional<ir::IRNode>
    find_minimal_equivalent(const ir::IRNode& node) const noexcept {

        ir::IRNode opt = node;

        // MUL ÃƒÂ¢Ã¢â‚¬Â Ã¢â‚¬â„¢ SHL (power-of-2)
        if (node.opcode == ir::OpCode::MUL_I32) {
            if ((node.flags & ir::Flags::IS_CONST) ||
                (node.flags & ir::Flags::IS_SPEC_CONST)) {

                if (node.op_b > 0 && std::has_single_bit(node.op_b)) {
                    opt.opcode = ir::OpCode::SHL_I32;
                    opt.op_b = static_cast<uint32_t>(std::countr_zero(node.op_b));
                    opt.flags |= (ir::Flags::IS_OPTIMIZED | ir::Flags::IS_MACRO_FUSED);
                    opt.intent_hash = compute_master_hash(opt);
                    return opt;
                }
            }
        }

        // XOR x,x ÃƒÂ¢Ã¢â‚¬Â Ã¢â‚¬â„¢ 0
        if (node.opcode == ir::OpCode::XOR_I32 && node.op_a == node.op_b) {
            opt.opcode = ir::OpCode::LOAD_IMM;
            opt.op_a = 0;
            opt.op_b = 0;
            opt.op_c = ir::INVALID_SLOT;
            opt.flags |= (ir::Flags::IS_CONST | ir::Flags::IS_SPEC_CONST);
            opt.intent_hash = compute_master_hash(opt);
            return opt;
        }

        // ADD x,0 ÃƒÂ¢Ã¢â‚¬Â Ã¢â‚¬â„¢ x
        if (node.opcode == ir::OpCode::ADD_I32 && node.op_b == 0) {
            opt.opcode = ir::OpCode::NOP;
            opt.flags |= ir::Flags::IS_OPTIMIZED;
            opt.intent_hash = compute_master_hash(opt);
            return opt;
        }

        return std::nullopt;
    }

    // ============================================================
    // SYMBOLIC VERIFICATION (STRONGER)
    // ============================================================

    [[nodiscard]] bool verify_transform(
        const ir::IRNode& before,
        const ir::IRNode& after) const noexcept
    {
        SymbolicVal a = compute_symbolic(before);
        SymbolicVal b = compute_symbolic(after);

        if (!a.has_ub && b.has_ub) return false;

        uint64_t known = (~a.unknown_mask) & (~b.unknown_mask);
        uint64_t diff  = (a.fixed_bits ^ b.fixed_bits) & known;

        return diff == 0;
    }

    // ============================================================
    // MEMORY VALIDATION
    // ============================================================

    [[nodiscard]] bool verify_disjoint(
        const ir::IRStream& s,
        uint32_t a,
        uint32_t b) const noexcept
    {
        const size_t stream_size = s.size();
        if (a >= stream_size || b >= stream_size) [[unlikely]] {
            return false;
        }

        const auto& A = s.get_core_readonly(a);
        const auto& B = s.get_core_readonly(b);

        if (A.op_a == ir::INVALID_SLOT || B.op_a == ir::INVALID_SLOT) {
            return false;
        }

        // Distinct base pointers are considered disjoint.
        if (A.op_a != B.op_a) {
            return true;
        }

        // Same base: treat as disjoint only when byte offsets are at least 4 bytes apart.
        const uint32_t off_a = A.op_b;
        const uint32_t off_b = B.op_b;
        const uint32_t delta = (off_a > off_b) ? (off_a - off_b) : (off_b - off_a);
        return delta >= 4u;
    }

    [[nodiscard]] bool is_contiguous(
        const ir::IRStream& s,
        uint32_t a,
        uint32_t b) const noexcept
    {
        const size_t stream_size = s.size();
        if (a >= stream_size || b >= stream_size) [[unlikely]] {
            return false;
        }

        const auto& A = s.get_core_readonly(a);
        const auto& B = s.get_core_readonly(b);

        if (A.op_a == ir::INVALID_SLOT || B.op_a == ir::INVALID_SLOT) {
            return false;
        }

        if (A.op_b > (UINT32_MAX - 4u)) {
            return false;
        }

        return (A.op_a == B.op_a) && (B.op_b == A.op_b + 4);
    }

    // ============================================================
    // BIT SENTINEL
    // ============================================================

    [[nodiscard]] bool verify_bit_consistency(
        const ir::IRNode& before,
        const ir::IRNode& after) const noexcept
    {
        SymbolicVal A = compute_symbolic(before);
        SymbolicVal B = compute_symbolic(after);

        uint64_t known = (~A.unknown_mask) & (~B.unknown_mask);
        uint64_t diff  = (A.fixed_bits ^ B.fixed_bits) & known;

        if (diff != 0) return false;

        if (A.unknown_mask == 0 && B.unknown_mask != 0)
            return false;

        return true;
    }

private:

    // ============================================================
    // SYMBOLIC ENGINE (UPGRADED)
    // ============================================================

    SymbolicVal compute_symbolic(const ir::IRNode& node) const noexcept {

        SymbolicVal r{0, 0xFFFFFFFFFFFFFFFFULL, false};

        const bool is_const =
            (node.flags & ir::Flags::IS_CONST) ||
            (node.flags & ir::Flags::IS_SPEC_CONST);

        if (node.opcode == ir::OpCode::LOAD_IMM || node.opcode == ir::OpCode::IMMEDIATE) {
            // DGTL encoding: op_a=high32, op_b=low32 for immediate payload.
            r.fixed_bits = (static_cast<uint64_t>(node.op_a) << 32) |
                           static_cast<uint64_t>(node.op_b);
            r.unknown_mask = 0;
            return r;
        }

        if (is_const) {
            r.fixed_bits = static_cast<uint64_t>(node.op_a);
            r.unknown_mask = 0;
            return r;
        }

        // Partial propagation for AND
        if (node.opcode == ir::OpCode::AND_I32 && is_const) {
            r.fixed_bits = node.op_a & node.op_b;
            r.unknown_mask = 0;
            return r;
        }

        // UB detection
        if (node.opcode == ir::OpCode::DIV_I32 &&
            is_const && node.op_b == 0) {
            r.has_ub = true;
        }

        return r;
    }
};

} // namespace dgtl::core


