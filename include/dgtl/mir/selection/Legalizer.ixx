/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Component:   Target-Aware HIR Legalizer
 * Standards:   C++26 / Clang 18+ / MSVC 19.40+ / GCC 14+
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

module;

#include <cstdlib>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstddef>
#include <optional>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

export module dgtl.mir.legalizer;

// Import native compiler base interfaces and sibling partitions
import dgtl.ir;

// Explicitly import target descriptions to resolve core::TargetMachine
import dgtl.target;
import dgtl.mir.runtime_policy;

/**
 * @brief Target-aware HIR legalizer.
 *
 * Canonical HIR -> target-aware legal HIR only.
 *
 * This stage:
 *   - may canonicalize/decompose target-illegal HIR;
 *   - may consult the selected TargetMachine for legality;
 *   - never creates target MachineOpcode values;
 *   - never assigns physical registers;
 *   - never constructs common MIR.
 *
 * Instruction selection owns the HIR -> common-MIR transition.
 */
export namespace dgtl::mir::lowering {

struct WideScalar128Operands {
    uint32_t lhs_lo = ir::INVALID_SLOT;
    uint32_t lhs_hi = ir::INVALID_SLOT;
    uint32_t rhs_lo = ir::INVALID_SLOT;
    uint32_t rhs_hi = ir::INVALID_SLOT;
    uint32_t result_lo = ir::INVALID_SLOT;
    uint32_t result_hi = ir::INVALID_SLOT;
};

struct LegalizationRemark {
    size_t node_index = 0;
    [[maybe_unused]] std::string_view detail; 
};

struct LegalizationResult {
    ir::IRStream stream;
    bool changed = false;
    std::vector<LegalizationRemark> remarks;

    [[nodiscard]] bool succeeded() const noexcept {
        return !stream.is_null();
    }
};

class MachineLegalizer {
public:
    static constexpr uint32_t kScalarWidth128TransportFlag = ir::ExtendedFlags::VEC_WIDTH_128;

    void register_scalar128_operands(size_t node_index, const WideScalar128Operands& operands) {
        if (node_index >= wide_scalar_nodes_.size()) {
            wide_scalar_nodes_.resize(node_index + 1);
        }
        wide_scalar_nodes_[node_index] = operands;
    }

    [[nodiscard]] static bool requires_legalization(const ir::IRNode& node,
                                                    const core::TargetMachine& target) noexcept {
        if (is_branch_hint_opcode(node.opcode)) {
            return true;
        }

        // 128-bit scalar arithmetic is represented by the transport flag on the
        // semantic IR node. Legalization is target-driven: native 128-bit targets
        // keep the operation intact; narrower targets require decomposition.
        if ((node.logic_flags & kScalarWidth128TransportFlag) != 0 &&
            !target.description().supports_scalar_width(128)) {
            switch (node.opcode) {
                case ir::OpCode::ADD_I64:
                case ir::OpCode::SUB_I64:
                    return true;
                default:
                    break;
            }
        }

        // Half-precision promotion: targets without native FP16/BF16 promote to F32.
        if (node.opcode == ir::OpCode::ADD_BF16) {
            const bool native_fp16 = target.supports_scalar_width(16) ||
                                     target.family() == core::TargetFamily::CUDA ||
                                     target.family() == core::TargetFamily::ROCm;
            if (!native_fp16) {
                return true;
            }
        }

        // Vector narrowing: 512-bit vector operations decompose to 256-bit on AVX2/narrow SIMD
        if ((node.opcode == ir::OpCode::VEC_ADD_F32_512 || node.opcode == ir::OpCode::VEC_MUL_F32_512) &&
            !target.description().supports_vector_width(512) && target.supports_vector_width(256)) {
            return true;
        }

        // Fixed 512-bit vector contract enforcement
        if (!target.description().supports_vector_width(512)) {
            switch (node.opcode) {
                case ir::OpCode::VEC_ADD_F32_512:
                case ir::OpCode::VEC_MUL_F32_512:
                case ir::OpCode::GATHER_F32_512:
                case ir::OpCode::STORE_V512:
                    return true;
                default:
                    break;
            }
        }

        if (node.opcode == ir::OpCode::FMA_F32) {
            const bool supports_fma = target.supports_fma() ||
                                      target.family() == core::TargetFamily::CUDA ||
                                      target.family() == core::TargetFamily::ROCm;
            if (!supports_fma) {
                return true;
            }
        }

        return false;
    }

    [[nodiscard]] static bool requires_runtime_fallback(const ir::IRNode& node,
                                                        const core::TargetMachine& target) noexcept {
        if (node.opcode == ir::OpCode::FMA_F32) {
            const bool supports_fma = target.supports_fma() ||
                                      target.family() == core::TargetFamily::CUDA ||
                                      target.family() == core::TargetFamily::ROCm;
            if (!supports_fma) {
                return runtime_policy::should_route_unhandled_to_runtime_call(target, node.opcode);
            }
        }

        if (!target.description().supports_vector_width(512)) {
            switch (node.opcode) {
                case ir::OpCode::VEC_ADD_F32_512:
                case ir::OpCode::VEC_MUL_F32_512:
                case ir::OpCode::GATHER_F32_512:
                case ir::OpCode::STORE_V512:
                    return runtime_policy::should_route_unhandled_to_runtime_call(target, node.opcode);
                default:
                    break;
            }
        }

        return false;
    }

    [[nodiscard]] LegalizationResult legalize(const ir::IRStream& input, const core::TargetMachine& target) const {
        if (input.is_null()) {
            return {};
        }

        constexpr std::size_t kExpansionFactor = 6u;
        constexpr std::size_t kExpansionSlack = 32u;

        if (input.size() >
            (std::numeric_limits<std::size_t>::max() - kExpansionSlack) /
                kExpansionFactor) {
            LegalizationResult failed;
            failed.remarks.push_back(
                LegalizationRemark{0u, "legalization input is too large for bounded transactional capacity"});
            return failed;
        }

        LegalizationResult result;
        result.stream = ir::IRStream::allocate_transactional(
            input.size() * kExpansionFactor + kExpansionSlack);

        uint32_t next_temp = compute_next_virtual_id(input);

        result.remarks.reserve(input.size() / 4);

        auto propagate_node_metadata = [&](size_t in_idx, size_t out_idx) {
            const auto overflow = input.get_call_overflow_args(in_idx);
            if (!overflow.empty()) {
                result.stream.set_call_overflow_args(out_idx, overflow);
            }
            const auto words = input.get_call_abi_layout_words(in_idx);
            if (!words.empty()) {
                result.stream.set_call_abi_layout_words(out_idx, words);
            }
            const auto res_ids = input.get_result_ids(in_idx);
            if (!res_ids.empty()) {
                result.stream.set_result_ids(out_idx, res_ids);
                for (uint32_t rid : res_ids) {
                    if (rid != ir::INVALID_SLOT && rid != 0u) {
                        result.stream.set_result_type(rid, input.get_result_type_code(rid));
                    }
                }
            }
            const auto op_overflow = input.get_operand_overflow(in_idx);
            if (!op_overflow.empty()) {
                result.stream.set_operand_overflow(out_idx, op_overflow);
            }
            const auto effects = input.get_node_effects(in_idx);
            if (effects != 0) {
                result.stream.set_node_effects(out_idx, effects);
            }
        };

        for (size_t i = 0; i < input.size(); ++i) {
            const ir::IRNode node = input.get_node(i);
            if (node.result_id != ir::INVALID_SLOT && node.result_id != 0u) {
                result.stream.set_result_type(node.result_id, input.get_result_type_code(node.result_id));
            }

            if (is_branch_hint_opcode(node.opcode)) {
                result.changed = true;
                ir::IRNode canonical = node;
                canonical.opcode = ir::OpCode::BRANCH_COND;
                canonical.flags |= ir::Flags::IS_BRANCH;
                canonical.refresh_intent_hash();
                result.stream.push_back(canonical);
                propagate_node_metadata(i, result.stream.size() - 1);
                result.remarks.push_back(LegalizationRemark{i, "canonicalized BRANCH_LIKELY/BRANCH_UNLIKELY hint opcode to BRANCH_COND for backend-stable lowering"});
                continue;
            }

            const bool is_scalar128_expansion =
                (node.logic_flags & kScalarWidth128TransportFlag) != 0 &&
                !target.description().supports_scalar_width(128) &&
                (node.opcode == ir::OpCode::ADD_I64 || node.opcode == ir::OpCode::SUB_I64);

            if (is_scalar128_expansion) {
                if (i >= wide_scalar_nodes_.size() ||
                    wide_scalar_nodes_[i].lhs_lo == ir::INVALID_SLOT ||
                    wide_scalar_nodes_[i].lhs_hi == ir::INVALID_SLOT ||
                    wide_scalar_nodes_[i].rhs_lo == ir::INVALID_SLOT ||
                    wide_scalar_nodes_[i].rhs_hi == ir::INVALID_SLOT ||
                    wide_scalar_nodes_[i].result_lo == ir::INVALID_SLOT ||
                    wide_scalar_nodes_[i].result_hi == ir::INVALID_SLOT) {
                    result.remarks.push_back(LegalizationRemark{
                        i,
                        "128-bit scalar legalization requested without registered wide operand metadata; node preserved"
                    });
                    result.stream.push_back(node);
                    propagate_node_metadata(i, result.stream.size() - 1);
                    continue;
                }

                if (next_temp == ir::INVALID_SLOT) {
                    result.stream.push_back(node);
                    propagate_node_metadata(i, result.stream.size() - 1);
                    result.remarks.push_back(LegalizationRemark{
                        i,
                        "128-bit scalar legalization aborted: virtual-register ID space is exhausted; node preserved"
                    });
                    continue;
                }

                result.changed = true;
                expand_scalar128(node, wide_scalar_nodes_[i], next_temp, result.stream);
                result.remarks.push_back(LegalizationRemark{
                    i,
                    "expanded non-native 128-bit scalar arithmetic into low/high halves with carry chain"
                });
                continue;
            }

            const bool native_fp16 = target.supports_scalar_width(16) ||
                                     target.family() == core::TargetFamily::CUDA ||
                                     target.family() == core::TargetFamily::ROCm;
            const bool is_fp16_promotion =
                (node.opcode == ir::OpCode::ADD_BF16) && !native_fp16;

            if (is_fp16_promotion) {
                if (next_temp == ir::INVALID_SLOT) {
                    result.stream.push_back(node);
                    propagate_node_metadata(i, result.stream.size() - 1);
                    result.remarks.push_back(LegalizationRemark{
                        i,
                        "FP16 promotion aborted: virtual-register ID space is exhausted; node preserved"
                    });
                    continue;
                }

                result.changed = true;
                expand_fp16_to_fp32_promotion(node, next_temp, result.stream);
                result.remarks.push_back(LegalizationRemark{
                    i,
                    "promoted ADD_BF16 to F32 arithmetic with convert-in/convert-out"
                });
                continue;
            }

            const bool is_vec512_narrowing =
                (node.opcode == ir::OpCode::VEC_ADD_F32_512 || node.opcode == ir::OpCode::VEC_MUL_F32_512) &&
                !target.description().supports_vector_width(512) &&
                target.supports_vector_width(256);

            if (is_vec512_narrowing) {
                if (next_temp == ir::INVALID_SLOT) {
                    result.stream.push_back(node);
                    propagate_node_metadata(i, result.stream.size() - 1);
                    continue;
                }

                result.changed = true;
                expand_vec512_to_vec256(node, next_temp, result.stream);
                result.remarks.push_back(LegalizationRemark{
                    i,
                    "narrowed 512-bit vector operation into dual 256-bit vector operations"
                });
                continue;
            }

            if (requires_legalization(node, target)) {
                result.stream.push_back(node);
                propagate_node_metadata(i, result.stream.size() - 1);
                if (requires_runtime_fallback(node, target)) {
                    result.remarks.push_back(LegalizationRemark{
                        i,
                        "target-specific legality mismatch: generated form exists for the ISA family, but the selected TargetMachine requires runtime fallback"
                    });
                } else {
                    result.remarks.push_back(LegalizationRemark{
                        i,
                        "target requires legalization for this opcode, but no structural lowering recipe or runtime fallback exists; node preserved"
                    });
                }
                continue;
            }

            const runtime_policy::LegalizationRoute route =
                runtime_policy::classify(target, node.opcode);

            if (route == runtime_policy::LegalizationRoute::RuntimeCall) {
                result.stream.push_back(node);
                propagate_node_metadata(i, result.stream.size() - 1);
                result.remarks.push_back(LegalizationRemark{
                    i,
                    "target-aware runtime-backed policy: no direct generated ISel mapping exists for this target; preserved for runtime-call lowering"
                });
                continue;
            }

            if (route == runtime_policy::LegalizationRoute::Unhandled) {
                result.stream.push_back(node);
                propagate_node_metadata(i, result.stream.size() - 1);
                result.remarks.push_back(LegalizationRemark{
                    i,
                    "opcode has neither a target-specific generated ISel mapping nor a runtime contract; preserved for verifier/diagnostic handling"
                });
                continue;
            }

            result.stream.push_back(node);
            propagate_node_metadata(i, result.stream.size() - 1);
        }

        return result;
    }

private:
    std::vector<WideScalar128Operands> wide_scalar_nodes_;

    [[nodiscard]] static bool is_branch_hint_opcode(ir::OpCode opcode) noexcept {
        return opcode == ir::OpCode::BRANCH_LIKELY || opcode == ir::OpCode::BRANCH_UNLIKELY;
    }

    [[nodiscard]] static uint32_t compute_next_virtual_id(
        const ir::IRStream& stream) noexcept {
        const uint32_t max_id = stream.get_max_id();

        if (max_id == ir::INVALID_SLOT || max_id >= 0xFFFFFFFEu) {
            return ir::INVALID_SLOT;
        }
        return max_id + 1u;
    }

    static void clear_width_transport_flag(ir::IRNode& node) noexcept {
        node.logic_flags &= ~kScalarWidth128TransportFlag;
    }

    static void expand_scalar128(const ir::IRNode& original,
                                 const WideScalar128Operands& pair,
                                 uint32_t& next_temp,
                                 ir::IRStream& out) {
        ir::IRNode low_op = original;
        low_op.result_id = next_temp++;
        low_op.op_a = pair.lhs_lo;
        low_op.op_b = pair.rhs_lo;
        low_op.op_c = ir::INVALID_SLOT;
        clear_width_transport_flag(low_op);
        low_op.refresh_intent_hash();
        out.push_back(low_op);
        
        uint32_t low_result_temp = low_op.result_id;

        ir::IRNode carry_detect{};
        carry_detect.opcode = ir::OpCode::CMP_LT_I32;
        carry_detect.flags = ir::Flags::NONE;
        carry_detect.result_id = next_temp++;
        if (original.opcode == ir::OpCode::SUB_I64) {
            carry_detect.op_a = pair.lhs_lo;
            carry_detect.op_b = pair.rhs_lo;
        } else {
            carry_detect.op_a = low_result_temp;
            carry_detect.op_b = pair.lhs_lo;
        }
        carry_detect.op_c = ir::INVALID_SLOT;
        carry_detect.logic_flags = ir::ExtendedFlags::LATENCY_CRITICAL;
        carry_detect.refresh_intent_hash();
        out.push_back(carry_detect);
        
        uint32_t carry_bit = carry_detect.result_id;

        ir::IRNode high_partial = original;
        high_partial.result_id = next_temp++;
        high_partial.op_a = pair.lhs_hi;
        high_partial.op_b = pair.rhs_hi;
        high_partial.op_c = ir::INVALID_SLOT;
        clear_width_transport_flag(high_partial);
        high_partial.refresh_intent_hash();
        out.push_back(high_partial);
        
        uint32_t high_partial_result = high_partial.result_id;

        ir::IRNode high_with_carry = original;
        high_with_carry.opcode = (original.opcode == ir::OpCode::SUB_I64)
            ? ir::OpCode::SUB_I64
            : ir::OpCode::ADD_I64;
        high_with_carry.result_id = next_temp++;
        high_with_carry.op_a = high_partial_result;
        high_with_carry.op_b = carry_bit;
        high_with_carry.op_c = ir::INVALID_SLOT;
        clear_width_transport_flag(high_with_carry);
        high_with_carry.refresh_intent_hash();
        out.push_back(high_with_carry);
        
        uint32_t high_result_temp = high_with_carry.result_id;

        ir::IRNode move_low{};
        move_low.opcode = ir::OpCode::REG_MOVE;
        move_low.flags = ir::Flags::NONE;
        move_low.result_id = pair.result_lo;
        move_low.op_a = low_result_temp;
        move_low.op_b = ir::INVALID_SLOT;
        move_low.op_c = ir::INVALID_SLOT;
        move_low.logic_flags = ir::ExtendedFlags::DETERMINISTIC;
        move_low.refresh_intent_hash();
        out.push_back(move_low);

        ir::IRNode move_high{};
        move_high.opcode = ir::OpCode::REG_MOVE;
        move_high.flags = ir::Flags::NONE;
        move_high.result_id = pair.result_hi;
        move_high.op_a = high_result_temp;
        move_high.op_b = ir::INVALID_SLOT;
        move_high.op_c = ir::INVALID_SLOT;
        move_high.logic_flags = ir::ExtendedFlags::DETERMINISTIC;
        move_high.refresh_intent_hash();
        out.push_back(move_high);
    }

    static void expand_fp16_to_fp32_promotion(const ir::IRNode& original,
                                              uint32_t& next_temp,
                                              ir::IRStream& out) {
        ir::IRNode cvt_a{};
        cvt_a.opcode = ir::OpCode::FCVTL_F16_TO_F32;
        cvt_a.result_id = next_temp++;
        cvt_a.op_a = original.op_a;
        cvt_a.op_b = ir::INVALID_SLOT;
        cvt_a.op_c = ir::INVALID_SLOT;
        cvt_a.refresh_intent_hash();
        out.push_back(cvt_a);

        ir::IRNode cvt_b{};
        cvt_b.opcode = ir::OpCode::FCVTL_F16_TO_F32;
        cvt_b.result_id = next_temp++;
        cvt_b.op_a = original.op_b;
        cvt_b.op_b = ir::INVALID_SLOT;
        cvt_b.op_c = ir::INVALID_SLOT;
        cvt_b.refresh_intent_hash();
        out.push_back(cvt_b);

        ir::IRNode add_f32{};
        add_f32.opcode = ir::OpCode::ADD_F32;
        add_f32.result_id = next_temp++;
        add_f32.op_a = cvt_a.result_id;
        add_f32.op_b = cvt_b.result_id;
        add_f32.op_c = ir::INVALID_SLOT;
        add_f32.refresh_intent_hash();
        out.push_back(add_f32);

        ir::IRNode cvt_res{};
        cvt_res.opcode = ir::OpCode::FCVTN_F32_TO_F16;
        cvt_res.result_id = original.result_id;
        cvt_res.op_a = add_f32.result_id;
        cvt_res.op_b = ir::INVALID_SLOT;
        cvt_res.op_c = ir::INVALID_SLOT;
        cvt_res.refresh_intent_hash();
        out.push_back(cvt_res);
    }

    static void expand_vec512_to_vec256(const ir::IRNode& original,
                                        uint32_t& next_temp,
                                        ir::IRStream& out) {
        ir::IRNode low_half = original;
        low_half.result_id = next_temp++;
        low_half.logic_flags = (low_half.logic_flags & ~ir::ExtendedFlags::VEC_WIDTH_512) | ir::ExtendedFlags::VEC_WIDTH_256;
        low_half.refresh_intent_hash();
        out.push_back(low_half);

        ir::IRNode high_half = original;
        high_half.result_id = original.result_id;
        high_half.logic_flags = (high_half.logic_flags & ~ir::ExtendedFlags::VEC_WIDTH_512) | ir::ExtendedFlags::VEC_WIDTH_256;
        high_half.refresh_intent_hash();
        out.push_back(high_half);
    }
};

using Legalizer = MachineLegalizer;

} // namespace dgtl::mir::lowering