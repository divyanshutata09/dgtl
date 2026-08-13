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

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

export module dgtl.ir_core.legalizer;

// Import native compiler base interfaces and sibling partitions
import dgtl.ir_core;

// FIXED: Explicitly import target descriptions to resolve core::TargetMachine
import dgtl.target;
import dgtl.ir_core.runtime_policy; // Assumes Runtime_Legalization_Policy is exported here

/**
 * @brief Machine Legalizer
 *
 * Implements the legalization and decomposition phase of compilation. Translates 
 * unsupported high-level operations (e.g. 128-bit wide scalar math on hardware supporting 
 * at most 64-bit scalars, branch hints) into sequences of legal primitive operations.
 * Manages virtual registers allocation during decomposition and carry-chain propagation.
 */
export namespace dgtl::isa::lowering {

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

    [[nodiscard]] static bool requires_legalization(const ir::IRNode& node, const core::TargetMachine& target) noexcept {
        if (is_branch_hint_opcode(node.opcode)) {
            return true;
        }
        if ((node.logic_flags & kScalarWidth128TransportFlag) == 0) {
            return false;
        }
        if (target.description().supports_scalar_width(128)) {
            return false;
        }
        switch (node.opcode) {
            case ir::OpCode::ADD_I64:
            case ir::OpCode::SUB_I64:
                return true;
            default:
                return false;
        }
    }

    [[nodiscard]] LegalizationResult legalize(const ir::IRStream& input, const core::TargetMachine& target) const {
        LegalizationResult result{ir::IRStream::allocate_transactional((input.size() * 6) + 32), false, {}};
        uint32_t next_temp = compute_next_virtual_id(input);

        result.remarks.reserve(input.size() / 4);

        for (size_t i = 0; i < input.size(); ++i) {
            const ir::IRNode node = input.get_node(i);
            if (runtime_policy::should_route_unhandled_to_runtime_call(node.opcode)) {
                result.stream.push_back(node);
                result.remarks.push_back(LegalizationRemark{i, "runtime-backed policy: opcode has no direct ISel mapping; selector fallback will route to Call via runtime contract"});
                continue;
            }
            if (is_branch_hint_opcode(node.opcode)) {
                result.changed = true;
                ir::IRNode canonical = node;
                canonical.opcode = ir::OpCode::BRANCH_COND;
                canonical.flags |= ir::Flags::IS_BRANCH;
                canonical.refresh_intent_hash();
                result.stream.push_back(canonical);
                result.remarks.push_back(LegalizationRemark{i, "canonicalized BRANCH_LIKELY/BRANCH_UNLIKELY hint opcode to BRANCH_COND for backend-stable lowering"});
                continue;
            }

            if (!requires_legalization(node, target)) {
                result.stream.push_back(node);
                continue;
            }

            if (i >= wide_scalar_nodes_.size() || wide_scalar_nodes_[i].lhs_lo == ir::INVALID_SLOT) {
                result.remarks.push_back(LegalizationRemark{i, "128-bit scalar legalization requested without registered wide operand metadata; node preserved"});
                result.stream.push_back(node);
                continue;
            }

            result.changed = true;
            expand_scalar128(node, wide_scalar_nodes_[i], next_temp, result.stream);
            result.remarks.push_back(LegalizationRemark{i, "expanded non-native 128-bit scalar arithmetic into low/high halves with carry chain"});
        }

        return result;
    }

private:
    std::vector<WideScalar128Operands> wide_scalar_nodes_;

    [[nodiscard]] static bool is_branch_hint_opcode(ir::OpCode opcode) noexcept {
        return opcode == ir::OpCode::BRANCH_LIKELY || opcode == ir::OpCode::BRANCH_UNLIKELY;
    }

    [[nodiscard]] static uint32_t compute_next_virtual_id(const ir::IRStream& stream) noexcept {
        uint32_t max_id = 0;
        for (size_t i = 0; i < stream.size(); ++i) {
            const ir::IRNode node = stream.get_node(i);
            max_id = std::max({max_id, node.result_id, node.op_a, node.op_b, node.op_c});
        }
        return (max_id == ir::INVALID_SLOT) ? 1u : (max_id + 1u);
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
        carry_detect.opcode = (original.opcode == ir::OpCode::ADD_I64) ? ir::OpCode::CMP_LT_I32 : ir::OpCode::CMP_GT_I32;
        carry_detect.flags = ir::Flags::NONE;
        carry_detect.result_id = next_temp++;
        carry_detect.op_a = low_result_temp;
        carry_detect.op_b = pair.lhs_lo;
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
        high_with_carry.opcode = ir::OpCode::ADD_I64;
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
};

} // namespace dgtl::isa::lowering