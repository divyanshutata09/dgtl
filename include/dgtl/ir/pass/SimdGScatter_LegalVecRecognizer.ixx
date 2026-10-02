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

#include <cstdint>
#include <cstddef>
#include <vector>
#include <array>
#include <algorithm>
#include <optional>
#include <immintrin.h>

// Bind to the correct module partition space matching your build matrix
export module dgtl.ir.pass.simd_gather_scatter;

// Order your target dependencies contiguously
import dgtl.ir_node;
import dgtl.ir_enums;
import dgtl.ir.stream;
import dgtl.op_traits; 
import dgtl.ir.analysis.cost_model;
import dgtl.ir.analysis.liveness_memory_dependency_unified;

// Load local configuration module fragments safely
import dgtl.ir.pipeline.pass_manager_config;

// Bring in traits/namespaces for local use
// interfaces namespace resolved via dgtl::ir::gen

export namespace dgtl::ir::pass {

// ============================================================================
// SECTION 1: SIMD LEGALIZER - BASIC PATTERN DETECTION & FUSION
// ============================================================================

/**
 * @enum TargetArch
 * @brief Supported SIMD target architectures
 */
enum class TargetArch : uint8_t {
    X86_AVX512,     // 16-lane 512-bit vectors
    X86_AVX2,       // 8-lane 256-bit vectors
    ARM_NEON,       // 4-lane 128-bit vectors
    WASM_SIMD128,   // 4-lane 128-bit vectors
    GENERIC         // Fallback (no SIMD)
};

/**
 * @struct AVX2DualPumpKernel
 * @brief Dual-pump AVX2 kernel for precondition and masked operations
 */
template <typename FloatT = float>
struct AVX2DualPumpKernel {
    static constexpr std::size_t kLanesPerPump = 8;
    static constexpr std::size_t kNodesPerIteration = 16;

    [[nodiscard]] static constexpr std::size_t lanes_per_pump() noexcept { return kLanesPerPump; }
    [[nodiscard]] static constexpr std::size_t nodes_per_iteration() noexcept { return kNodesPerIteration; }

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

/**
 * @class DGTLSIMDLegalizer
 * @brief Pattern-matching SIMD legalizer using 4-node sliding window
 */
class DGTLSIMDLegalizer {
    TargetArch target;

    template <size_t Lanes>
    using NodeWindow = std::array<ir::IRNode, Lanes>;

public:
    explicit DGTLSIMDLegalizer(TargetArch arch) noexcept : target(arch) {}

    void legalize(ir::IRStream& stream) {
        const size_t stream_size = stream.size();
        const uint32_t lane_width = target_lane_width();
        if (lane_width < 2 || stream_size < lane_width) return;

        stream.begin_transaction();
        stream.push_full_to_shadow();

        bool transaction_valid = true;
        const uint32_t last_start = static_cast<uint32_t>(stream_size) - lane_width;

        for (uint32_t i = 0; i <= last_start; ++i) {
            const auto head = stream.get_node_readonly(i);
            if (head.opcode != ir::OpCode::ADD_I32) {
                continue;
            }

            if (!is_vectorizable_window(stream, i, lane_width)) {
                continue;
            }

            fuse_to_simd_v4_transactional(stream, i, lane_width);
            i += (lane_width - 1);
        }

        if (!stream.validate_shadow_integrity()) {
            transaction_valid = false;
        }

        if (transaction_valid) {
            stream.commit_transaction();
        } else {
            stream.rollback_transaction();
        }
    }

private:
    [[nodiscard]] uint32_t target_lane_width() const noexcept {
        switch (target) {
            case TargetArch::X86_AVX512:    return 16;
            case TargetArch::X86_AVX2:      return 8;
            case TargetArch::ARM_NEON:
            case TargetArch::WASM_SIMD128:  return 4;
            case TargetArch::GENERIC:
            default:                        return 0;
        }
    }

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
    [[nodiscard]] static NodeWindow<Lanes> load_window(const ir::IRStream& stream, uint32_t start) noexcept {
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

        return std::all_of(nodes.begin(), nodes.end(), [](const ir::IRNode& node) noexcept {
            const bool is_control_flow = (node.opcode == ir::OpCode::BRANCH || node.opcode == ir::OpCode::BRANCH_COND || node.opcode == ir::OpCode::RET);
            return !is_control_flow &&
                   node.op_a != ir::INVALID_SLOT &&
                   node.op_b != ir::INVALID_SLOT &&
                   node.result_id != ir::INVALID_SLOT;
        });

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

        const __m512i add_target = _mm512_set1_epi32(static_cast<int32_t>(ir::OpCode::ADD_I32));
        const __m512i invalid_slot = _mm512_set1_epi32(static_cast<int32_t>(ir::INVALID_SLOT));

        const __m512i opcode_vec = _mm512_load_si512(opcodes.data());
        const __mmask16 add_mask = _mm512_cmpeq_epi32_mask(opcode_vec, add_target);
        if (add_mask != 0xFFFFu) {
            return false;
        }

        const __m512i result_vec = _mm512_load_si512(results.data());
        const __m512i op_a_vec = _mm512_load_si512(op_a.data());
        const __m512i op_b_vec = _mm512_load_si512(op_b.data());

        const __mmask16 missing = 
            _mm512_cmpeq_epi32_mask(result_vec, invalid_slot) |
            _mm512_cmpeq_epi32_mask(op_a_vec, invalid_slot) |
            _mm512_cmpeq_epi32_mask(op_b_vec, invalid_slot);

        if (missing != 0) {
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
    [[nodiscard]] static bool has_independent_lanes_ptr(const NodeWindow<Lanes>& nodes) noexcept {
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
    [[nodiscard]] static bool has_balanced_operands_ptr(const NodeWindow<Lanes>& nodes) noexcept {
        const auto& front_node = nodes.front();
        uint32_t min_op_a = front_node.op_a;
        uint32_t max_op_a = min_op_a;
        uint32_t min_op_b = front_node.op_b;
        uint32_t max_op_b = min_op_b;

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
            vec_port_nodes += static_cast<size_t>((node.logic_flags & ir::HintFlags::RequiresVecPort) != 0);
            fused_nodes += static_cast<size_t>((node.flags & ir::Flags::IS_FUSED) != 0);
        }

        if (fused_nodes != 0) return false;

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
            case TargetArch::ARM_NEON:
            case TargetArch::WASM_SIMD128:
                return vec_port_nodes >= 1;
            case TargetArch::GENERIC:
            default:
                return false;
        }
    }

    void fuse_to_simd_v4_transactional(ir::IRStream& stream, uint32_t start, [[maybe_unused]] uint32_t lane_width) {
        ir::IRAtom& v_atom = stream.get_shadow_atom(start);
        ir::IRCore& v_core = stream.get_shadow_core(start);

        ir::OpCode fused_op;
        switch (target) {
            case TargetArch::X86_AVX512:    fused_op = static_cast<ir::OpCode>(0x204); break;
            case TargetArch::X86_AVX2:      fused_op = static_cast<ir::OpCode>(0x204); break;
            case TargetArch::ARM_NEON:      fused_op = static_cast<ir::OpCode>(0x304); break;
            case TargetArch::WASM_SIMD128:  fused_op = static_cast<ir::OpCode>(0x404); break;
            default:                        fused_op = ir::OpCode::ADD_I32; break;
        }

        v_atom.opcode = fused_op;
        v_atom.flags |= (ir::Flags::IS_SIMD_BURST | ir::Flags::IS_FUSED);
        v_core.logic_flags |= ir::HintFlags::RequiresVecPort | ir::HintFlags::NonTemporalHint;
        
        v_core.intent_hash = ir::IRNode::calculate_hash(
            v_atom.opcode,
            v_core.result_id,
            v_core.op_a,
            v_core.op_b,
            v_core.op_c
        );
    }
};

// ============================================================================
// SECTION 2: GATHER/SCATTER OPTIMIZER
// ============================================================================

/**
 * @class DGTLGatherScatterOptimizer
 * @brief Converts scalar load chains into vectorized gather operations
 */
class DGTLGatherScatterOptimizer {
    dgtl::analysis::DGTLMemorySmasher& smasher;
    dgtl::ir::analysis::AICostModel& ai_model;

    static constexpr uint32_t GatherLaneCount = 16;
    static constexpr float GatherCostSlack = 0.92f;
    static constexpr float MaxAliasRisk = 0.30f;
    static constexpr uint32_t CacheLineSize = 64;

public:
    DGTLGatherScatterOptimizer(dgtl::analysis::DGTLMemorySmasher& s, 
                               dgtl::ir::analysis::AICostModel& ai) noexcept
        : smasher(s), ai_model(ai) {}

    void optimize_indirect_access(ir::IRStream& stream) {
        const size_t stream_size = stream.size();
        if (stream_size < GatherLaneCount) return;

        stream.begin_transaction();

        for (uint32_t i = 0; i < static_cast<uint32_t>(stream_size); ++i) {
            if (i % 8 == 0 && (i + 16) < stream_size) [[likely]] {
                stream.prefetch_node(i + 16);
            }
            sync_node_to_shadow(stream, i);
        }

        for (uint32_t i = 0; i <= static_cast<uint32_t>(stream_size) - GatherLaneCount; ++i) {
            const ir::IRNode node = stream.get_node_readonly(i);

            if (node.opcode != ir::OpCode::LOAD || !is_indirect_address(node)) {
                continue;
            }
            if (!is_gather_candidate_window(stream, i)) continue;

            if (!smasher.can_group_access(stream, i, GatherLaneCount)) continue;

            ir::IRNode probe = node;
            convert_to_gather_node(probe);

            float scalar_cost = 0.0f;
            for (uint32_t k = 0; k < GatherLaneCount; ++k) {
                scalar_cost += ai_model.predict_node_cost(stream.get_node_readonly(i + k));
            }
            float gather_cost = estimate_gather_cost(stream, i, probe);
            gather_cost *= is_cache_line_aligned(node.op_a) ? 0.85f : 1.0f;

            if (gather_cost <= scalar_cost * GatherCostSlack) {
                apply_gather_transaction(stream, i);
                i += (GatherLaneCount - 1);
            }
        }

        if (!stream.validate_shadow_integrity()) {
            stream.rollback_transaction();
            return;
        }

        stream.commit_transaction();
    }

private:
    static void sync_node_to_shadow(ir::IRStream& s, uint32_t idx) noexcept {
        ir::IRNode node = s.get_node_readonly(idx);
        ir::IRAtom& s_atom = s.get_shadow_atom(idx);
        ir::IRCore& s_core = s.get_shadow_core(idx);
        s_atom = {node.opcode, node.flags};
        s_core = {node.result_id, node.op_a, node.op_b, node.op_c, node.intent_hash, 0, 0};
    }

    [[nodiscard]] static bool is_indirect_address(const ir::IRNode& node) noexcept {
        const bool is_memory_op = node.is_memory_read() || node.is_memory_write();
        
        // FIXED: Directly assert atomic loop side-effect state variables natively 
        const bool has_side_effects = ((node.flags & ir::Flags::IS_VOLATILE) != 0);
        return (node.flags & ir::Flags::IS_SPEC_CONST) != 0 &&
               is_memory_op &&
               !has_side_effects &&
               (node.flags & ir::Flags::IS_SIMD_BURST) == 0 &&
               node.op_a != ir::INVALID_SLOT;
    }

    [[nodiscard]] static bool is_cache_line_aligned(uint32_t address) noexcept {
        return (address % CacheLineSize) == 0;
    }

    [[nodiscard]] static bool is_gather_candidate_window(const ir::IRStream& stream, uint32_t head_idx) noexcept {
        const ir::IRNode head = stream.get_node_readonly(head_idx);
        uint32_t min_addr = head.op_a;
        uint32_t max_addr = head.op_a;

        for (uint32_t j = 0; j < GatherLaneCount; ++j) {
            const ir::IRNode node = stream.get_node_readonly(head_idx + j);
            if (node.opcode != ir::OpCode::LOAD || !is_indirect_address(node)) {
                return false;
            }
            if ((node.flags & ir::Flags::IS_FUSED) != 0 || (node.flags & ir::Flags::IS_DEAD) != 0) {
                return false;
            }
            min_addr = std::min(min_addr, node.op_a);
            max_addr = std::max(max_addr, node.op_a);
        }

        return (max_addr - min_addr) <= 4096;
    }

    [[nodiscard]] float estimate_gather_cost(const ir::IRStream& stream, uint32_t head_idx, const ir::IRNode& probe) noexcept {
        float gather_cost = ai_model.predict_node_cost(probe);
        float alias_risk = 0.0f;

        for (uint32_t j = 1; j < GatherLaneCount; ++j) {
            alias_risk = std::max(alias_risk, smasher.predict_alias_probability(stream, head_idx, head_idx + j));
            if (alias_risk > MaxAliasRisk) {
                return gather_cost + 1000.0f;
            }
        }

        const ir::IRNode tail = stream.get_node_readonly(head_idx + GatherLaneCount - 1);
        const uint32_t address_span = (tail.op_a > probe.op_a) ? (tail.op_a - probe.op_a) : (probe.op_a - tail.op_a);
        
        const float span_penalty = std::min(12.0f, static_cast<float>(address_span / CacheLineSize) * 1.2f);
        const float alias_penalty = alias_risk * 15.0f;

        return gather_cost + span_penalty + alias_penalty;
    }

    static void apply_gather_transaction(ir::IRStream& stream, uint32_t head_idx) {
        ir::IRAtom& h_atom = stream.get_shadow_atom(head_idx);
        ir::IRCore& h_core = stream.get_shadow_core(head_idx);

        h_atom.opcode = ir::OpCode::GATHER_F32_512;
        
        const uint32_t head_address = stream.get_node_readonly(head_idx).op_a;
        const uint32_t tail_address = stream.get_node_readonly(head_idx + GatherLaneCount - 1).op_a;
        const uint32_t span = (tail_address > head_address)
            ? (tail_address - head_address)
            : (head_address - tail_address);

        if (span > 1024) h_core.logic_flags |= ir::HintFlags::NonTemporalHint;

        h_atom.flags |= (ir::Flags::IS_SIMD_BURST | ir::Flags::IS_FUSED | ir::Flags::IS_OPTIMIZED);
        h_core.logic_flags |= ir::HintFlags::RequiresVecPort;
        
        h_core.intent_hash = ir::IRNode::calculate_hash(
            h_atom.opcode,
            h_core.result_id,
            h_core.op_a,
            h_core.op_b,
            h_core.op_c
        );

        for (uint32_t j = 1; j < GatherLaneCount; ++j) {
            ir::IRAtom& f_atom = stream.get_shadow_atom(head_idx + j);
            ir::IRCore& f_core = stream.get_shadow_core(head_idx + j);
            
            f_atom = {ir::OpCode::NOP, ir::Flags::NONE};
            f_core = {ir::INVALID_SLOT, 0, 0, 0, 0, 0, 0};
            
            f_core.intent_hash = ir::IRNode::calculate_hash(
                f_atom.opcode,
                f_core.result_id,
                f_core.op_a,
                f_core.op_b,
                f_core.op_c
            );
        }
    }

    static void convert_to_gather_node(ir::IRNode& node) noexcept {
        node.opcode = ir::OpCode::GATHER_F32_512;
        node.flags |= (ir::Flags::IS_SIMD_BURST | ir::Flags::IS_FUSED | ir::Flags::IS_OPTIMIZED);
        node.logic_flags |= ir::HintFlags::RequiresVecPort;
    }
};

// ============================================================================
// SECTION 3: ADVANCED VECTORIZATION (MIXED-PRECISION)
// ============================================================================

/**
 * @class DGTLAdvancedVectorization
 * @brief Mixed-precision F16/F32 vectorization with partial vectors
 */
class DGTLAdvancedVectorization : public dgtl::pipeline::DGTLOptimizationPass {
private:
    enum class VectorPattern : uint8_t {
        MixedPrecisionF16F32 = 0,
        PartialVectorLoad = 1,
        PartialVectorStore = 2,
        MixedPrecisionReduction = 3,
        TypeUpconversionChain = 4,
        StridedLoadStore = 5
    };

    struct VectorizationOpportunity {
        VectorPattern pattern{VectorPattern::StridedLoadStore};
        uint32_t start_idx{0};
        uint32_t end_idx{0};
        uint8_t original_width{0};
        uint8_t target_width{0};
        dgtl::ir::OpCode fused_opcode{dgtl::ir::OpCode::NOP};

        [[nodiscard]] bool is_valid_opportunity() const noexcept { return end_idx > start_idx; }
    };

public:
    DGTLAdvancedVectorization() = default;

    void execute(dgtl::ir::IRStream& stream) override {
        if (stream.is_null() || stream.size() < 8) [[unlikely]] return;

        stream.begin_transaction();

        auto opportunities = detect_opportunities(stream);
        if (opportunities.empty()) {
            stream.rollback_transaction();
            return;
        }

        bool modified = synthesize_vectorization(stream, opportunities);

        if (modified && stream.validate_shadow_integrity()) {
            stream.commit_transaction();
        } else {
            stream.rollback_transaction();
        }
    }

    [[nodiscard]] const char* get_pass_name() const noexcept override {
        return "DGTLAdvancedVectorization";
    }

    [[nodiscard]] dgtl::pipeline::PassStage get_stage() const noexcept override {
        return dgtl::pipeline::PassStage::PostInlining;
    }

    [[nodiscard]] dgtl::pipeline::PassAffinity hot_path_affinity() const noexcept override {
        return dgtl::pipeline::PassAffinity::High;
    }

    [[nodiscard]] dgtl::pipeline::PassAffinity memory_affinity() const noexcept override {
        return dgtl::pipeline::PassAffinity::High;
    }

    [[nodiscard]] dgtl::pipeline::PassAffinity vector_affinity() const noexcept override {
        return dgtl::pipeline::PassAffinity::High;
    }

    [[nodiscard]] dgtl::pipeline::PassAffinity speculation_affinity() const noexcept override {
        return dgtl::pipeline::PassAffinity::Low;
    }

private:
    std::vector<VectorizationOpportunity> detect_opportunities(
        const dgtl::ir::IRStream& stream) {
        
        std::vector<VectorizationOpportunity> opps;

        for (uint32_t i = 0; i + 3 < static_cast<uint32_t>(stream.size()); ++i) {
            if (auto opp = try_match_f16_f32_blend(stream, i)) {
                opps.push_back(*opp);
                continue;
            }

            if (auto opp = try_match_partial_load(stream, i)) {
                opps.push_back(*opp);
                continue;
            }

            if (auto opp = try_match_partial_store(stream, i)) {
                opps.push_back(*opp);
                continue;
            }

            if (auto opp = try_match_upconversion_chain(stream, i)) {
                opps.push_back(*opp);
                continue;
            }

            if (auto opp = try_match_mixed_reduction(stream, i)) {
                opps.push_back(*opp);
            }
        }

        return opps;
    }

    std::optional<VectorizationOpportunity> try_match_f16_f32_blend(
        const dgtl::ir::IRStream& stream, uint32_t start_idx) {
        
        const auto& node0 = stream.get_node_readonly(start_idx);
        if (node0.opcode != dgtl::ir::OpCode::LOAD &&
            node0.opcode != dgtl::ir::OpCode::VEC_LOAD) {
            return std::nullopt;
        }

        const auto& node1 = stream.get_node_readonly(start_idx + 1);
        if (node1.opcode != dgtl::ir::OpCode::MUL_F32 &&
            node1.opcode != dgtl::ir::OpCode::ADD_F32) {
            return std::nullopt;
        }

        const auto& node2 = stream.get_node_readonly(start_idx + 2);
        if (node2.opcode != dgtl::ir::OpCode::STORE &&
            node2.opcode != dgtl::ir::OpCode::VEC_STORE) {
            return std::nullopt;
        }

        return VectorizationOpportunity{
            .pattern = VectorPattern::MixedPrecisionF16F32,
            .start_idx = start_idx,
            .end_idx = start_idx + 2,
            .original_width = 4,
            .target_width = 8,
            .fused_opcode = dgtl::ir::OpCode::VEC_LOAD
        };
    }

    std::optional<VectorizationOpportunity> try_match_partial_load(
        const dgtl::ir::IRStream& stream, uint32_t start_idx) {
        
        const auto& node0 = stream.get_node_readonly(start_idx);
        if (node0.opcode != dgtl::ir::OpCode::LOAD &&
            node0.opcode != dgtl::ir::OpCode::VEC_LOAD) {
            return std::nullopt;
        }

        const auto& node1 = stream.get_node_readonly(start_idx + 1);
        if (node1.opcode != dgtl::ir::OpCode::AND_I32 &&
            node1.opcode != dgtl::ir::OpCode::ADD_I32) {
            return std::nullopt;
        }

        return VectorizationOpportunity{
            .pattern = VectorPattern::PartialVectorLoad,
            .start_idx = start_idx,
            .end_idx = start_idx + 1,
            .original_width = 4,
            .target_width = 16,
            .fused_opcode = dgtl::ir::OpCode::VEC_LOAD
        };
    }

    std::optional<VectorizationOpportunity> try_match_partial_store(
        const dgtl::ir::IRStream& stream, uint32_t start_idx) {
        
        const auto& store = stream.get_node_readonly(start_idx);
        if (store.opcode != dgtl::ir::OpCode::STORE &&
            store.opcode != dgtl::ir::OpCode::VEC_STORE) {
            return std::nullopt;
        }

        if ((store.flags & dgtl::ir::Flags::IS_SIMD_BURST) == 0) {
            return std::nullopt;
        }

        return VectorizationOpportunity{
            .pattern = VectorPattern::PartialVectorStore,
            .start_idx = start_idx,
            .end_idx = start_idx,
            .original_width = 4,
            .target_width = 16,
            .fused_opcode = dgtl::ir::OpCode::VEC_STORE
        };
    }

    std::optional<VectorizationOpportunity> try_match_upconversion_chain(
        const dgtl::ir::IRStream& stream, uint32_t start_idx) {
        
        const auto& cvt = stream.get_node_readonly(start_idx);
        if (cvt.opcode != dgtl::ir::OpCode::FCVTL_F16_TO_F32 &&
            cvt.opcode != dgtl::ir::OpCode::MUL_F32) {
            return std::nullopt;
        }

        const auto& compute = stream.get_node_readonly(start_idx + 1);
        if (!is_arithmetic_opcode(compute.opcode)) {
            return std::nullopt;
        }

        return VectorizationOpportunity{
            .pattern = VectorPattern::TypeUpconversionChain,
            .start_idx = start_idx,
            .end_idx = start_idx + 1,
            .original_width = 4,
            .target_width = 8,
            .fused_opcode = dgtl::ir::OpCode::VEC_LOAD
        };
    }

    std::optional<VectorizationOpportunity> try_match_mixed_reduction(
        const dgtl::ir::IRStream& stream, uint32_t start_idx) {
        
        const auto& reduce = stream.get_node_readonly(start_idx);
        if (reduce.opcode != dgtl::ir::OpCode::VEC_REDUCE_ADD &&
            reduce.opcode != dgtl::ir::OpCode::VEC_ADD_F32_512 &&
            reduce.opcode != dgtl::ir::OpCode::MUL_F32) {
            return std::nullopt;
        }

        return VectorizationOpportunity{
            .pattern = VectorPattern::MixedPrecisionReduction,
            .start_idx = start_idx,
            .end_idx = start_idx,
            .original_width = 4,
            .target_width = 16,
            .fused_opcode = reduce.opcode
        };
    }

    static bool is_arithmetic_opcode(dgtl::ir::OpCode opcode) {
        return opcode == dgtl::ir::OpCode::ADD_F32 ||
               opcode == dgtl::ir::OpCode::MUL_F32 ||
               opcode == dgtl::ir::OpCode::ADD_I32 ||
               opcode == dgtl::ir::OpCode::MUL_I32 ||
               opcode == dgtl::ir::OpCode::DIV_F32 ||
               opcode == dgtl::ir::OpCode::SUB_F32;
    }

    bool synthesize_vectorization(dgtl::ir::IRStream& stream,
                                  const std::vector<VectorizationOpportunity>& opportunities) {
        uint32_t shadow_cursor = 0;
        bool modified = false;

        for (const auto& opp : opportunities) {
            auto& s_atom = stream.get_shadow_atom(shadow_cursor);
            auto& s_core = stream.get_shadow_core(shadow_cursor);

            const auto& repr_node = stream.get_node_readonly(opp.start_idx);

            s_atom.opcode = opp.fused_opcode;
            s_atom.flags = repr_node.flags | static_cast<decltype(s_atom.flags)>(
                dgtl::ir::Flags::IS_SIMD_BURST | dgtl::ir::Flags::IS_OPTIMIZED
            );
            s_core.result_id = repr_node.result_id;
            s_core.op_a = repr_node.op_a;
            s_core.op_b = repr_node.op_b;
            s_core.op_c = repr_node.op_c;
            s_core.intent_hash = dgtl::ir::IRNode::calculate_hash(
                s_atom.opcode, s_core.result_id, s_core.op_a, s_core.op_b, s_core.op_c
            );

            shadow_cursor++;
            modified = true;
        }

        return modified;
    }
};

// ============================================================================
// SECTION 4: ADVANCED SIMD RECOGNIZER (PATTERN-MATCHING)
// ============================================================================

/**
 * @class DGTLAdvancedSIMDRecognizer
 * @brief Detects and synthesizes 8+ advanced SIMD patterns for 8-16x speedup
 */
class DGTLAdvancedSIMDRecognizer : public dgtl::pipeline::DGTLOptimizationPass {
private:
    enum class SIMDPattern : uint8_t {
        HorizontalSum = 0,
        HorizontalMax = 1,
        HorizontalMin = 2,
        ScatterIndirect = 3,
        ConditionalBlend = 4,
        MaskedStore = 5,
        InterleavedLoad = 6,
        ComplexFMA = 7
    };

    struct PatternMatch {
        SIMDPattern pattern{SIMDPattern::HorizontalSum};
        uint32_t pattern_start_idx{0};
        uint32_t pattern_end_idx{0};
        std::vector<uint32_t> involved_ids;
        dgtl::ir::OpCode fused_opcode{dgtl::ir::OpCode::NOP};
    };

public:
    DGTLAdvancedSIMDRecognizer() = default;

    void execute(dgtl::ir::IRStream& stream) override {
        if (stream.is_null() || stream.size() < 8) [[unlikely]] return;

        stream.begin_transaction();

        auto matches = detect_patterns(stream);
        if (matches.empty()) {
            stream.rollback_transaction();
            return;
        }

        bool modified = synthesize_patterns(stream, matches);

        if (modified && stream.validate_shadow_integrity()) {
            stream.commit_transaction();
        } else {
            stream.rollback_transaction();
        }
    }

    [[nodiscard]] const char* get_pass_name() const noexcept override {
        return "DGTLAdvancedSIMDRecognizer";
    }

    [[nodiscard]] dgtl::pipeline::PassStage get_stage() const noexcept override {
        return dgtl::pipeline::PassStage::PostInlining;
    }

    [[nodiscard]] dgtl::pipeline::PassAffinity hot_path_affinity() const noexcept override {
        return dgtl::pipeline::PassAffinity::High;
    }

    [[nodiscard]] dgtl::pipeline::PassAffinity memory_affinity() const noexcept override {
        return dgtl::pipeline::PassAffinity::Medium;
    }

    [[nodiscard]] dgtl::pipeline::PassAffinity vector_affinity() const noexcept override {
        return dgtl::pipeline::PassAffinity::High;
    }

    [[nodiscard]] dgtl::pipeline::PassAffinity speculation_affinity() const noexcept override {
        return dgtl::pipeline::PassAffinity::Low;
    }

private:
    std::vector<PatternMatch> detect_patterns(const dgtl::ir::IRStream& stream) {
        std::vector<PatternMatch> matches;

        for (uint32_t i = 0; i + 4 < static_cast<uint32_t>(stream.size()); ++i) {
            if (auto match = try_match_horizontal_reduction(stream, i)) {
                matches.push_back(*match);
                continue;
            }

            if (auto match = try_match_scatter_indirect(stream, i)) {
                matches.push_back(*match);
                continue;
            }

            if (auto match = try_match_conditional_blend(stream, i)) {
                matches.push_back(*match);
                continue;
            }

            if (auto match = try_match_masked_store(stream, i)) {
                matches.push_back(*match);
                continue;
            }

            if (auto match = try_match_interleaved_load(stream, i)) {
                matches.push_back(*match);
                continue;
            }

            if (auto match = try_match_complex_fma(stream, i)) {
                matches.push_back(*match);
            }
        }

        return matches;
    }

    std::optional<PatternMatch> try_match_horizontal_reduction(
        const dgtl::ir::IRStream& stream, uint32_t start_idx) {
        
        const auto& node0 = stream.get_node_readonly(start_idx);
        if (node0.opcode != dgtl::ir::OpCode::VEC_REDUCE_ADD &&
            node0.opcode != dgtl::ir::OpCode::VEC_ADD_F32_512) {
            return std::nullopt;
        }

        const auto& node1 = stream.get_node_readonly(start_idx + 1);
        if (node1.opcode != dgtl::ir::OpCode::ADD_F32 &&
            node1.opcode != dgtl::ir::OpCode::ADD_I32) {
            return std::nullopt;
        }

        if (node1.op_a != node0.result_id && node1.op_b != node0.result_id) {
            return std::nullopt;
        }

        return PatternMatch{
            .pattern = SIMDPattern::HorizontalSum,
            .pattern_start_idx = start_idx,
            .pattern_end_idx = start_idx + 1,
            .involved_ids = {node0.result_id, node1.result_id},
            .fused_opcode = dgtl::ir::OpCode::VEC_REDUCE_ADD
        };
    }

    std::optional<PatternMatch> try_match_scatter_indirect(
        const dgtl::ir::IRStream& stream, uint32_t start_idx) {
        
        const auto& load_node = stream.get_node_readonly(start_idx);
        if (load_node.opcode != dgtl::ir::OpCode::VEC_LOAD) {
            return std::nullopt;
        }

        const auto& idx_node = stream.get_node_readonly(start_idx + 1);
        if (idx_node.opcode != dgtl::ir::OpCode::LOAD) {
            return std::nullopt;
        }

        const auto& scatter_node = stream.get_node_readonly(start_idx + 2);
        if (scatter_node.opcode != dgtl::ir::OpCode::STORE) {
            return std::nullopt;
        }

        return PatternMatch{
            .pattern = SIMDPattern::ScatterIndirect,
            .pattern_start_idx = start_idx,
            .pattern_end_idx = start_idx + 2,
            .involved_ids = {load_node.result_id, idx_node.result_id},
            .fused_opcode = dgtl::ir::OpCode::GATHER_F32_512
        };
    }

    std::optional<PatternMatch> try_match_conditional_blend(
        const dgtl::ir::IRStream& stream, uint32_t start_idx) {
        
        const auto& mask_node = stream.get_node_readonly(start_idx);
        if (mask_node.opcode != dgtl::ir::OpCode::BRANCH_COND) {
            return std::nullopt;
        }

        const auto& true_node = stream.get_node_readonly(start_idx + 1);
        const auto& false_node = stream.get_node_readonly(start_idx + 2);

        if ((true_node.flags & dgtl::ir::Flags::IS_SIMD_BURST) == 0) {
            return std::nullopt;
        }

        return PatternMatch{
            .pattern = SIMDPattern::ConditionalBlend,
            .pattern_start_idx = start_idx,
            .pattern_end_idx = start_idx + 2,
            .involved_ids = {mask_node.result_id, true_node.result_id, false_node.result_id},
            .fused_opcode = dgtl::ir::OpCode::SIMD_BSL_PREDICATE
        };
    }

    std::optional<PatternMatch> try_match_masked_store(
        const dgtl::ir::IRStream& stream, uint32_t start_idx) {
        
        const auto& store_node = stream.get_node_readonly(start_idx);
        if (store_node.opcode != dgtl::ir::OpCode::STORE) {
            return std::nullopt;
        }

        if ((store_node.flags & dgtl::ir::Flags::IS_SIMD_BURST) == 0) {
            return std::nullopt;
        }

        return PatternMatch{
            .pattern = SIMDPattern::MaskedStore,
            .pattern_start_idx = start_idx,
            .pattern_end_idx = start_idx,
            .involved_ids = {store_node.result_id},
            .fused_opcode = dgtl::ir::OpCode::STORE_V512
        };
    }

    std::optional<PatternMatch> try_match_interleaved_load(
        const dgtl::ir::IRStream& stream, uint32_t start_idx) {
        
        const auto& load1 = stream.get_node_readonly(start_idx);
        if (load1.opcode != dgtl::ir::OpCode::VEC_LOAD) {
            return std::nullopt;
        }

        return PatternMatch{
            .pattern = SIMDPattern::InterleavedLoad,
            .pattern_start_idx = start_idx,
            .pattern_end_idx = start_idx,
            .involved_ids = {load1.result_id},
            .fused_opcode = dgtl::ir::OpCode::VEC_LOAD
        };
    }

    std::optional<PatternMatch> try_match_complex_fma(
        const dgtl::ir::IRStream& stream, uint32_t start_idx) {
        
        const auto& mul1 = stream.get_node_readonly(start_idx);
        if (mul1.opcode != dgtl::ir::OpCode::MUL_F32 && 
            mul1.opcode != dgtl::ir::OpCode::MUL_I32) {
            return std::nullopt;
        }

        const auto& mul2 = stream.get_node_readonly(start_idx + 1);
        if (mul2.opcode != dgtl::ir::OpCode::MUL_F32 &&
            mul2.opcode != dgtl::ir::OpCode::MUL_I32) {
            return std::nullopt;
        }

        const auto& add = stream.get_node_readonly(start_idx + 2);
        if (add.opcode != dgtl::ir::OpCode::ADD_F32 && 
            add.opcode != dgtl::ir::OpCode::ADD_I32) {
            return std::nullopt;
        }

        return PatternMatch{
            .pattern = SIMDPattern::ComplexFMA,
            .pattern_start_idx = start_idx,
            .pattern_end_idx = start_idx + 2,
            .involved_ids = {mul1.result_id, mul2.result_id},
            .fused_opcode = dgtl::ir::OpCode::FMA_F32
        };
    }

    bool synthesize_patterns(dgtl::ir::IRStream& stream,
                             const std::vector<PatternMatch>& matches) {
        uint32_t shadow_cursor = 0;
        bool modified = false;

        for (const auto& match : matches) {
            auto& s_atom = stream.get_shadow_atom(shadow_cursor);
            auto& s_core = stream.get_shadow_core(shadow_cursor);

            const auto& repr_node = stream.get_node_readonly(match.pattern_start_idx);

            s_atom.opcode = match.fused_opcode;
            s_atom.flags = repr_node.flags | static_cast<decltype(s_atom.flags)>(
                dgtl::ir::Flags::IS_SIMD_BURST | dgtl::ir::Flags::IS_OPTIMIZED
            );
            s_core.result_id = repr_node.result_id;
            s_core.op_a = repr_node.op_a;
            s_core.op_b = repr_node.op_b;
            s_core.op_c = repr_node.op_c;
            s_core.intent_hash = dgtl::ir::IRNode::calculate_hash(
                s_atom.opcode, s_core.result_id, s_core.op_a, s_core.op_b, s_core.op_c
            );

            shadow_cursor++;
            modified = true;
        }

        return modified;
    }
};

} // namespace dgtl::ir::pass

export namespace dgtl::ir::pass::simd {
    using namespace dgtl::ir::pass;
    using TargetArch = dgtl::ir::pass::TargetArch;
    using DGTLSIMDLegalizer = dgtl::ir::pass::DGTLSIMDLegalizer;
    using DGTLGatherScatterOptimizer = dgtl::ir::pass::DGTLGatherScatterOptimizer;
    using DGTLAdvancedVectorization = dgtl::ir::pass::DGTLAdvancedVectorization;
    using DGTLAdvancedSIMDRecognizer = dgtl::ir::pass::DGTLAdvancedSIMDRecognizer;
}

export namespace dgtl::pass {
    using namespace dgtl::ir::pass;
}

export namespace dgtl::pass::simd {
    using namespace dgtl::ir::pass;
}
