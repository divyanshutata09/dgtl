/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 */

#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include <algorithm>

#include "dgtl/core/Target.hpp"
#include "dgtl/ir/base/IR_Stream.hpp"

namespace dgtl::isa::lowering {

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
    std::string detail;
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
        wide_scalar_nodes_[node_index] = operands;
    }

    [[nodiscard]] bool requires_legalization(const ir::IRNode& node, const core::TargetMachine& target) const noexcept {
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

        for (size_t i = 0; i < input.size(); ++i) {
            const ir::IRNode node = input.get_node(i);
            if (!requires_legalization(node, target)) {
                result.stream.push_back(node);
                continue;
            }

            const auto it = wide_scalar_nodes_.find(i);
            if (it == wide_scalar_nodes_.end()) {
                result.remarks.push_back(LegalizationRemark{i, "128-bit scalar legalization requested without registered wide operand metadata; node preserved"});
                result.stream.push_back(node);
                continue;
            }

            result.changed = true;
            expand_scalar128(node, it->second, next_temp, result.stream);
            result.remarks.push_back(LegalizationRemark{i, "expanded non-native 128-bit scalar arithmetic into low/high halves with carry chain"});
        }

        return result;
    }

private:
    std::unordered_map<size_t, WideScalar128Operands> wide_scalar_nodes_;

    [[nodiscard]] static uint32_t compute_next_virtual_id(const ir::IRStream& stream) noexcept {
        uint32_t max_id = 0;
        for (size_t i = 0; i < stream.size(); ++i) {
            const ir::IRNode node = stream.get_node(i);
            max_id = std::max(max_id, node.result_id);
            max_id = std::max(max_id, node.op_a);
            max_id = std::max(max_id, node.op_b);
            max_id = std::max(max_id, node.op_c);
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
        // Step 1: Compute low half (LHS_LO op RHS_LO)
        ir::IRNode low_op = original;
        low_op.result_id = next_temp++;  // Temporary for low result
        low_op.op_a = pair.lhs_lo;
        low_op.op_b = pair.rhs_lo;
        low_op.op_c = ir::INVALID_SLOT;
        clear_width_transport_flag(low_op);
        low_op.refresh_intent_hash();
        out.push_back(low_op);
        
        uint32_t low_result_temp = low_op.result_id;

        // Step 2: Detect carry from low half
        // Carry is set if: (for ADD) result < operand, or (for SUB) result > operand
        ir::IRNode carry_detect{};
        carry_detect.opcode = (original.opcode == ir::OpCode::ADD_I64) ? ir::OpCode::CMP_LT_I32 : ir::OpCode::CMP_GT_I32;
        carry_detect.flags = ir::Flags::NONE;
        carry_detect.result_id = next_temp++;  // Carry bit (boolean)
        carry_detect.op_a = low_result_temp;
        carry_detect.op_b = pair.lhs_lo;
        carry_detect.op_c = ir::INVALID_SLOT;
        carry_detect.logic_flags = ir::ExtendedFlags::LATENCY_CRITICAL;
        carry_detect.refresh_intent_hash();
        out.push_back(carry_detect);
        
        uint32_t carry_bit = carry_detect.result_id;

        // Step 3: Compute high half (LHS_HI op RHS_HI)
        ir::IRNode high_partial = original;
        high_partial.result_id = next_temp++;  // Temporary for high partial
        high_partial.op_a = pair.lhs_hi;
        high_partial.op_b = pair.rhs_hi;
        high_partial.op_c = ir::INVALID_SLOT;
        clear_width_transport_flag(high_partial);
        high_partial.refresh_intent_hash();
        out.push_back(high_partial);
        
        uint32_t high_partial_result = high_partial.result_id;

        // Step 4: Add carry to high result (HI_PARTIAL + CARRY)
        ir::IRNode high_with_carry = original;
        high_with_carry.opcode = ir::OpCode::ADD_I64;  // Always add carry, regardless of original op
        high_with_carry.result_id = next_temp++;  // Temporary for final high
        high_with_carry.op_a = high_partial_result;
        high_with_carry.op_b = carry_bit;  // Carry as operand
        high_with_carry.op_c = ir::INVALID_SLOT;
        clear_width_transport_flag(high_with_carry);
        high_with_carry.refresh_intent_hash();
        out.push_back(high_with_carry);
        
        uint32_t high_result_temp = high_with_carry.result_id;

        // Step 5: Move low result to final location
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

        // Step 6: Move high result to final location
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
