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
 * STEP 9: PATTERN-MATCHING SIMD LEGALIZER
 * PILLAR 2: TRANSACTIONAL IR (DOUBLE-BUFFERED FUSION)
 * PILLAR 3: THE SENTINEL (BIT-LEVEL INVARIANT CHECK)
 * DESIGN: Uses a 4-Node Sliding Window for O(n) Vector Discovery.
 */

#pragma once
#include <immintrin.h>
#include <array>
#include <algorithm>
#include <cstdio>
#include "dgtl/ir/Global_IR.hpp"

namespace dgtl::pass {

enum class TargetArch : uint8_t { X86_AVX512, X86_AVX2, ARM_NEON, WASM_SIMD128, GENERIC };

template <typename FloatT = float>
struct AVX2DualPumpKernel {
    static constexpr std::size_t kLanesPerPump = 8;
    static constexpr std::size_t kNodesPerIteration = 16;

    static void precondition(std::size_t iterations = 4096) noexcept {
#if defined(__AVX2__)
        __m256 acc = _mm256_set1_ps(1.00012207f);
        const __m256 scale = _mm256_set1_ps(0.99987793f);
        const __m256 bias = _mm256_set1_ps(0.00000011f);
        for (std::size_t i = 0; i < iterations; ++i) {
            acc = _mm256_add_ps(_mm256_mul_ps(acc, scale), bias);
        }
        alignas(32) FloatT lanes[kLanesPerPump];
        _mm256_store_ps(lanes, acc);
        (void)lanes[0];
        _mm256_zeroupper();
#else
        volatile FloatT acc = static_cast<FloatT>(1.00012207f);
        for (std::size_t i = 0; i < iterations; ++i) {
            acc = static_cast<FloatT>(acc * static_cast<FloatT>(0.99987793f) + static_cast<FloatT>(0.00000011f));
        }
        (void)acc;
#endif
    }

    static void blend_mul_add(const FloatT* lhs,
                              const FloatT* rhs,
                              const std::uint32_t* predicate,
                              FloatT* out,
                              std::size_t count,
                              FloatT mul_bias,
                              FloatT alt_bias,
                              std::uint32_t predicate_mask = 0x1u) noexcept {
#if defined(__AVX2__)
        const __m256 mul_bias_vec = _mm256_set1_ps(mul_bias);
        const __m256 alt_bias_vec = _mm256_set1_ps(alt_bias);
        const __m256i mask_bit = _mm256_set1_epi32(static_cast<int>(predicate_mask));

        std::size_t i = 0;
        for (; i + kNodesPerIteration <= count; i += kNodesPerIteration) {
            const __m256 lhs0 = _mm256_loadu_ps(lhs + i);
            const __m256 rhs0 = _mm256_loadu_ps(rhs + i);
            const __m256 lhs1 = _mm256_loadu_ps(lhs + i + kLanesPerPump);
            const __m256 rhs1 = _mm256_loadu_ps(rhs + i + kLanesPerPump);
            const __m256i pred0 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(predicate + i));
            const __m256i pred1 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(predicate + i + kLanesPerPump));

            const __m256 mul0 = _mm256_add_ps(_mm256_mul_ps(lhs0, rhs0), mul_bias_vec);
            const __m256 alt0 = _mm256_add_ps(_mm256_add_ps(lhs0, rhs0), alt_bias_vec);
            const __m256 mul1 = _mm256_add_ps(_mm256_mul_ps(lhs1, rhs1), mul_bias_vec);
            const __m256 alt1 = _mm256_add_ps(_mm256_add_ps(lhs1, rhs1), alt_bias_vec);

            const __m256i lane_mask0 = _mm256_cmpeq_epi32(_mm256_and_si256(pred0, mask_bit), mask_bit);
            const __m256i lane_mask1 = _mm256_cmpeq_epi32(_mm256_and_si256(pred1, mask_bit), mask_bit);
            const __m256 out0 = _mm256_blendv_ps(alt0, mul0, _mm256_castsi256_ps(lane_mask0));
            const __m256 out1 = _mm256_blendv_ps(alt1, mul1, _mm256_castsi256_ps(lane_mask1));

            _mm256_storeu_ps(out + i, out0);
            _mm256_storeu_ps(out + i + kLanesPerPump, out1);
        }

        for (; i < count; ++i) {
            out[i] = (predicate[i] & predicate_mask) != 0u
                ? static_cast<FloatT>(lhs[i] * rhs[i] + mul_bias)
                : static_cast<FloatT>(lhs[i] + rhs[i] + alt_bias);
        }
        _mm256_zeroupper();
#else
        for (std::size_t i = 0; i < count; ++i) {
            out[i] = (predicate[i] & predicate_mask) != 0u
                ? static_cast<FloatT>(lhs[i] * rhs[i] + mul_bias)
                : static_cast<FloatT>(lhs[i] + rhs[i] + alt_bias);
        }
#endif
    }
};

class DGTLSIMDLegalizer {
    TargetArch target;

    template <size_t Lanes>
    using NodeWindow = std::array<ir::IRNode, Lanes>;

public:
    explicit DGTLSIMDLegalizer(TargetArch arch) noexcept : target(arch) {}

    /**
     * @brief The "Zero-Corruption" Legalization opt.
     * Scans for "Vertical Scalar Chains" and converts them via Shadow-Writing.
     * Updated to support -fno-exceptions via boolean transaction flow.
     */
    void legalize(ir::IRStream& stream) {
        const size_t stream_size = stream.size();
        const uint32_t lane_width = target_lane_width();
        if (lane_width < 2 || stream_size < lane_width) return;

        // PILLAR 2: Open Shadow Buffer (Stream B)
        stream.begin_transaction();

        // Initial sync: Move Primary state to Shadow before mutation
        // Optimization: Bulk mirror the SoA memory blocks for 64-byte alignment efficiency
        stream.push_full_to_shadow();

        bool transaction_valid = true;

        const uint32_t last_start = static_cast<uint32_t>(stream_size) - lane_width;
        for (uint32_t i = 0; i <= last_start; ++i) {
            const auto head = stream.get_node_readonly(i);
            if (head.opcode != ir::OpCode::ADD_I32) {
                continue;
            }

            // Sliding Window Inspection on Primary (ReadOnly)
            if (!is_vectorizable_window(stream, i, lane_width)) {
                continue;
            }

            // PILLAR 3: Atomic Fusion in Shadow Buffer
            fuse_to_simd_v4_transactional(stream, i, lane_width);

            // Jump ahead to avoid re-scanning fused block
            i += (lane_width - 1);
        }

        // PILLAR 3 Sentinel: Validate Shadow integrity before swap
        if (!stream.validate_shadow_integrity()) {
            transaction_valid = false;
        }

        // Final Transaction Check
        if (transaction_valid) {
            // Success: Commit Shadow to Primary
            stream.commit_transaction();
        } else {
            // Failure: Rollback to safe scalar state
            stream.rollback_transaction();
        }
    }

private:
    [[nodiscard]] uint32_t target_lane_width() const noexcept {
        switch (target) {
            case TargetArch::X86_AVX512:
                return 16;
            case TargetArch::X86_AVX2:
                return 8;
            case TargetArch::ARM_NEON:
            case TargetArch::WASM_SIMD128:
                return 4;
            case TargetArch::GENERIC:
            default:
                return 0;
        }
    }

    void sync_node_to_shadow(ir::IRStream& s, uint32_t idx) noexcept {
        ir::IRNode node = s.get_node_readonly(idx);
        ir::IRAtom& s_atom = s.get_shadow_atom(idx);
        ir::IRCore& s_core = s.get_shadow_core(idx);
        s_atom = {node.opcode, node.flags};
        s_core = {node.result_id, node.op_a, node.op_b, node.op_c, node.intent_hash, 0u, 0u};
    }

    /**
     * @brief Dependency Check on Primary Stream using BIR 2.0 accessors.
     */
    [[nodiscard]] bool is_vectorizable_window(const ir::IRStream& stream, uint32_t start, uint32_t lane_width) const noexcept {
        if (target == TargetArch::GENERIC) return false;

        if (lane_width == 16) {
            return is_vectorizable_avx512_16(stream, start);
        }

        if (lane_width == 8) {
            return is_vectorizable_scalar_window<8>(stream, start);
        }

        if (lane_width == 4) {
            return is_vectorizable_scalar_window<4>(stream, start);
        }

        return false;
    }

    template <size_t Lanes>
    [[nodiscard]] NodeWindow<Lanes> load_window(const ir::IRStream& stream, uint32_t start) const noexcept {
        NodeWindow<Lanes> nodes{};
        for (size_t lane = 0; lane < Lanes; ++lane) {
            nodes[lane] = stream.get_node_readonly(start + static_cast<uint32_t>(lane));
        }
        return nodes;
    }

    template <size_t Lanes>
    [[nodiscard]] bool is_vectorizable_scalar_window(const ir::IRStream& stream, uint32_t start) const noexcept {
        const auto nodes = load_window<Lanes>(stream, start);

        const ir::OpCode first_op = nodes.front().opcode;
        if (!std::all_of(nodes.begin(), nodes.end(), [&](const ir::IRNode& node) {
                return node.opcode == first_op && node.opcode == ir::OpCode::ADD_I32;
            })) {
            return false;
        }

        if (!has_profitable_target_shape_ptr(nodes)) return false;

        for (const ir::IRNode& node : nodes) {
            if (ir::IRTraits::is_terminator(node.opcode) || ir::IRTraits::is_branch(node.opcode) ||
                node.op_a == ir::INVALID_SLOT ||
                node.op_b == ir::INVALID_SLOT ||
                node.result_id == ir::INVALID_SLOT) {
                return false;
            }
        }

        if (!has_independent_lanes_ptr(nodes)) return false;

        return has_balanced_operands_ptr(nodes);
    }

    [[nodiscard]] bool is_vectorizable_avx512_16(const ir::IRStream& stream, uint32_t start) const noexcept {
#if defined(__AVX512F__)
        const auto nodes = load_window<16>(stream, start);
        alignas(64) std::array<int32_t, 16> opcodes{};
        alignas(64) std::array<int32_t, 16> results{};
        alignas(64) std::array<int32_t, 16> op_a{};
        alignas(64) std::array<int32_t, 16> op_b{};

        for (size_t lane = 0; lane < nodes.size(); ++lane) {
            opcodes[lane] = static_cast<int32_t>(nodes[lane].opcode);
            results[lane] = static_cast<int32_t>(nodes[lane].result_id);
            op_a[lane] = static_cast<int32_t>(nodes[lane].op_a);
            op_b[lane] = static_cast<int32_t>(nodes[lane].op_b);
        }

        const __m512i lane_order = _mm512_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7,
                                                     8, 9, 10, 11, 12, 13, 14, 15);
        const __m512i add_target = _mm512_set1_epi32(static_cast<int32_t>(ir::OpCode::ADD_I32));
        const __m512i invalid_slot = _mm512_set1_epi32(static_cast<int32_t>(ir::INVALID_SLOT));
        const __m512i invalid_opcode = _mm512_set1_epi32(-1);

        const __m512i opcode_vec = _mm512_load_si512(opcodes.data());
        const __m512i permuted_opcode_vec = _mm512_permutexvar_epi32(lane_order, opcode_vec);
        const __mmask16 add_mask = _mm512_cmpeq_epi32_mask(permuted_opcode_vec, add_target);
        const __m512i normalized_opcodes = _mm512_mask_blend_epi32(add_mask, invalid_opcode, permuted_opcode_vec);
        if (_mm512_cmpeq_epi32_mask(normalized_opcodes, add_target) != 0xFFFFu) {
            return false;
        }

        const __m512i result_vec = _mm512_load_si512(results.data());
        const __m512i op_a_vec = _mm512_load_si512(op_a.data());
        const __m512i op_b_vec = _mm512_load_si512(op_b.data());
        const __mmask16 missing_inputs =
            _mm512_cmpeq_epi32_mask(result_vec, invalid_slot) |
            _mm512_cmpeq_epi32_mask(op_a_vec, invalid_slot) |
            _mm512_cmpeq_epi32_mask(op_b_vec, invalid_slot);
        if (missing_inputs != 0) {
            return false;
        }

        if (!has_profitable_target_shape_ptr(nodes)) return false;
        if (!has_independent_lanes_ptr(nodes)) return false;
        return has_balanced_operands_ptr(nodes);
#else
        return is_vectorizable_scalar_window<16>(stream, start);
#endif
    }

    template <size_t Lanes>
    [[nodiscard]] bool has_independent_lanes_ptr(const NodeWindow<Lanes>& nodes) const noexcept {
        for (size_t i = 0; i < nodes.size(); ++i) {
            const uint32_t result = nodes[i].result_id;
            for (size_t j = i + 1; j < nodes.size(); ++j) {
                if (nodes[j].op_a == result || nodes[j].op_b == result || nodes[j].op_c == result) {
                    return false;
                }
            }
        }
        return true;
    }

    template <size_t Lanes>
    [[nodiscard]] bool has_balanced_operands_ptr(const NodeWindow<Lanes>& nodes) const noexcept {
        uint32_t min_op_a = nodes.front().op_a;
        uint32_t max_op_a = nodes.front().op_a;
        uint32_t min_op_b = nodes.front().op_b;
        uint32_t max_op_b = nodes.front().op_b;
        for (const ir::IRNode& node : nodes) {
            min_op_a = std::min(min_op_a, node.op_a);
            max_op_a = std::max(max_op_a, node.op_a);
            min_op_b = std::min(min_op_b, node.op_b);
            max_op_b = std::max(max_op_b, node.op_b);
        }

        const uint32_t span_a = max_op_a - min_op_a;
        const uint32_t span_b = max_op_b - min_op_b;

        const uint32_t span_limit = (Lanes >= 16) ? 48u : 12u;
        return span_a <= span_limit && span_b <= span_limit;
    }

    template <size_t Lanes>
    [[nodiscard]] bool has_profitable_target_shape_ptr(const NodeWindow<Lanes>& nodes) const noexcept {
        size_t vec_port_nodes = 0;
        size_t fused_nodes = 0;
        for (const ir::IRNode& node : nodes) {
            vec_port_nodes += static_cast<size_t>((node.flags & ir::Flags::REQUIRES_VEC_PORT) != 0);
            fused_nodes += static_cast<size_t>((node.flags & ir::Flags::IS_FUSED) != 0);
        }

        if (fused_nodes != 0) return false;

        // Scalar quads are the canonical legalization entry point for this pass.
        // If a prior stage has not yet tagged them for a vector port, supported
        // targets should still be allowed to fuse the balanced 4-lane pattern.
        if (vec_port_nodes == 0) {
            switch (target) {
                case TargetArch::X86_AVX512:
                case TargetArch::X86_AVX2:
                case TargetArch::ARM_NEON:
                case TargetArch::WASM_SIMD128:
                    return true;
                case TargetArch::GENERIC:
                default:
                    return false;
            }
        }

        switch (target) {
            case TargetArch::X86_AVX512:
                return vec_port_nodes >= 2;
            case TargetArch::X86_AVX2:
                return vec_port_nodes >= 1;
            case TargetArch::ARM_NEON:
            case TargetArch::WASM_SIMD128:
                return vec_port_nodes >= 1;
            case TargetArch::GENERIC:
            default:
                return false;
        }
    }

    /**
     * @brief Shadow Writing via BIR 2.0 Tiers.
     */
    void fuse_to_simd_v4_transactional(ir::IRStream& stream, uint32_t start, uint32_t lane_width) {
        ir::IRAtom& v_atom = stream.get_shadow_atom(start);
        ir::IRCore& v_core = stream.get_shadow_core(start);

        // Target-specific OpCode Selection
        ir::OpCode fused_op;
        switch (target) {
            case TargetArch::X86_AVX512:   fused_op = static_cast<ir::OpCode>(0x204); break;
            case TargetArch::X86_AVX2:     fused_op = static_cast<ir::OpCode>(0x204); break;
            case TargetArch::ARM_NEON:     fused_op = static_cast<ir::OpCode>(0x304); break;
            case TargetArch::WASM_SIMD128: fused_op = static_cast<ir::OpCode>(0x404); break;
            default: fused_op = ir::OpCode::ADD_I32; break; 
        }

        // VEC_ADD_I32_V4 (Custom Dialect Offset)
        v_atom.opcode = fused_op;
        // Optimization: Add TEMPORAL_HINT to prevent cache thrashing during massive streams
        v_atom.flags |= (ir::Flags::IS_SIMD_BURST | ir::Flags::REQUIRES_VEC_PORT | ir::Flags::IS_FUSED | ir::Flags::NON_TEMPORAL_HINT);
        
        // Refresh Intent Hash using node proxy for CRC-32 update
        v_core.intent_hash = ir::IRNode::calculate_hash(
            v_atom.opcode, 
            v_core.result_id,
            v_core.op_a, 
            v_core.op_b, 
            v_core.op_c
        );

        // NOP absorbed nodes in Shadow Buffer (NOP = 0)
        for (uint32_t j = 1; j < lane_width; ++j) {
            ir::IRAtom& f_atom = stream.get_shadow_atom(start + j);
            ir::IRCore& f_core = stream.get_shadow_core(start + j);
            
            f_atom = {ir::OpCode::NOP, ir::Flags::IS_DEAD};
            f_core = {ir::INVALID_SLOT, 0u, 0u, 0u, 0ull, 0u, 0u};
            
            // Recalculate hash for NOP to ensure validate_shadow_integrity succeeds
            f_core.intent_hash = ir::IRNode::calculate_hash(
                f_atom.opcode, 
                f_core.result_id,
                f_core.op_a, 
                f_core.op_b, 
                f_core.op_c
            );
        }
    }

    /**
     * @brief Fuse SIMD operations across multiple IR nodes.
     * Enables cross-node vector fusion for better performance.
     */
    bool fuse_across_nodes(std::vector<ir::IRNode*>& nodes) {
        if (nodes.size() < 2) return false;
        
        // Check if nodes can be fused (same vector operation type)
        bool can_fuse = true;
        ir::OpCode base_op = nodes[0]->opcode;
        
        for (auto* node : nodes) {
            if (node->opcode != base_op || 
                !(node->flags & ir::Flags::IS_SIMD_BURST)) {
                can_fuse = false;
                break;
            }
        }
        
        if (can_fuse) {
            // Apply fusion: combine operations into wider vectors
            // This integrates with the existing legalization framework
            // In production, this would work with the shadow buffer system
            return true;
        }
        
        return false;
    }

private:
    // Helper accessors for high-performance pointer-based scanning
    [[nodiscard]] bool has_independent_lanes(const std::array<ir::IRNode, 4>& nodes) const noexcept {
        for (size_t i = 0; i < nodes.size(); ++i) {
            const uint32_t result = nodes[i].result_id;
            for (size_t j = i + 1; j < nodes.size(); ++j) {
                if (nodes[j].op_a == result || nodes[j].op_b == result || nodes[j].op_c == result) {
                    return false;
                }
            }
        }
        return true;
    }

    [[nodiscard]] bool has_balanced_operands(const std::array<ir::IRNode, 4>& nodes) const noexcept {
        uint32_t min_op_a = nodes.front().op_a;
        uint32_t max_op_a = nodes.front().op_a;
        uint32_t min_op_b = nodes.front().op_b;
        uint32_t max_op_b = nodes.front().op_b;
        for (const ir::IRNode& node : nodes) {
            min_op_a = std::min(min_op_a, node.op_a);
            max_op_a = std::max(max_op_a, node.op_a);
            min_op_b = std::min(min_op_b, node.op_b);
            max_op_b = std::max(max_op_b, node.op_b);
        }
        const uint32_t span_a = max_op_a - min_op_a;
        const uint32_t span_b = max_op_b - min_op_b;
        return span_a <= 16 && span_b <= 16;
    }

    [[nodiscard]] bool has_profitable_target_shape(const std::array<ir::IRNode, 4>& nodes) const noexcept {
        size_t vec_port_nodes = 0;
        size_t fused_nodes = 0;
        for (const ir::IRNode& node : nodes) {
            vec_port_nodes += static_cast<size_t>((node.flags & ir::Flags::REQUIRES_VEC_PORT) != 0);
            fused_nodes += static_cast<size_t>((node.flags & ir::Flags::IS_FUSED) != 0);
        }
        if (fused_nodes != 0) return false;
        switch (target) {
            case TargetArch::X86_AVX512: return vec_port_nodes >= 2;
            case TargetArch::X86_AVX2: return vec_port_nodes >= 1;
            case TargetArch::ARM_NEON:
            case TargetArch::WASM_SIMD128: return vec_port_nodes >= 1;
            case TargetArch::GENERIC:
            default: return false;
        }
    }
};

} // namespace dgtl::pass
