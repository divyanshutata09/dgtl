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

#include <type_traits>
#include <vector>
#include <cstdint>
#include <unordered_map>
#include <algorithm>

export module dgtl.ir_core.strike_live_manager;

import dgtl.ir_core;
import dgtl.ir_core.runtime_contract;
import dgtl.flat_map;

export namespace dgtl::isa::regalloc {

namespace ir = dgtl::ir;

struct LiveInterval {
    size_t start_node = 0;
    size_t end_node = 0;
    bool active = false;
};

class StrikeLiveManager {
    std::vector<LiveInterval> intervals_;

    template <typename LabelMapType>
    static bool lookup_label_index(const LabelMapType& label_map,
                                   uint32_t label,
                                   size_t& out_index) {
        if constexpr (std::is_pointer_v<decltype(label_map.find(label))>) {
            if (const auto* ptr = label_map.find(label)) {
                out_index = *ptr;
                return true;
            }
            return false;
        } else {
            auto it = label_map.find(label);
            if (it != label_map.end()) {
                out_index = it->second;
                return true;
            }
            return false;
        }
    }

    struct OperandUseMask {
        bool use_a = false;
        bool use_b = false;
        bool use_c = false;
    };

    /**
     * @brief Determines which operands are read as virtual registers by a given opcode.
     * Prevents operands containing immediate literal values or label offsets from being treated as registers.
     */
    [[nodiscard]] static OperandUseMask operand_uses(ir::OpCode opcode) noexcept {
        switch (opcode) {
        // Reads exactly 1 virtual register (op_a)
        case ir::OpCode::LOAD:
        case ir::OpCode::VOLATILE_LOAD:
        case ir::OpCode::STORE_V512:
        case ir::OpCode::RET:
        case ir::OpCode::BRANCH_COND:
        case ir::OpCode::BRANCH_LIKELY:
        case ir::OpCode::BRANCH_UNLIKELY:
        case ir::OpCode::CLFLUSH:
        case ir::OpCode::PREFETCH_T0:
            return {true, false, false};

        // Reads 2 virtual registers (op_a, op_b)
        case ir::OpCode::REG_MOVE:
        case ir::OpCode::STORE:
        case ir::OpCode::VOLATILE_STORE:
        case ir::OpCode::ADD_I32:
        case ir::OpCode::SUB_I32:
        case ir::OpCode::MUL_I32:
        case ir::OpCode::DIV_I32:
        case ir::OpCode::MOD_I32:
        case ir::OpCode::ADD_I64:
        case ir::OpCode::SUB_I64:
        case ir::OpCode::MUL_I64:
        case ir::OpCode::DIV_I64:
        case ir::OpCode::MOD_I64:
        case ir::OpCode::CMP_EQ_I32:
        case ir::OpCode::CMP_NE_I32:
        case ir::OpCode::CMP_LT_I32:
        case ir::OpCode::CMP_LE_I32:
        case ir::OpCode::CMP_GT_I32:
        case ir::OpCode::CMP_GE_I32:
        case ir::OpCode::AND_I32:
        case ir::OpCode::OR_I32:
        case ir::OpCode::XOR_I32:
        case ir::OpCode::SHL_I32:
        case ir::OpCode::SHR_I32:
        case ir::OpCode::AND_I64:
        case ir::OpCode::OR_I64:
        case ir::OpCode::XOR_I64:
        case ir::OpCode::SHL_I64:
        case ir::OpCode::SHR_I64:
        case ir::OpCode::ADD_F32:
        case ir::OpCode::SUB_F32:
        case ir::OpCode::MUL_F32:
        case ir::OpCode::DIV_F32:
        case ir::OpCode::ADD_F64:
        case ir::OpCode::SUB_F64:
        case ir::OpCode::MUL_F64:
        case ir::OpCode::DIV_F64:
        case ir::OpCode::VEC_ADD_F32_512:
        case ir::OpCode::VEC_MUL_F32_512:
        case ir::OpCode::GATHER_F32_512:
        case ir::OpCode::GATHER_16:
        case ir::OpCode::BRANCH_COMPARE_FUSED:
        case ir::OpCode::GEP:
        case ir::OpCode::LEA:
        case ir::OpCode::PTR_DIFF:
        case ir::OpCode::PTR_TO_INT:
        case ir::OpCode::INT_TO_PTR:
        case ir::OpCode::PTR_CAST:
            return {true, true, false};

        // Reads 3 virtual registers (op_a, op_b, op_c)
        case ir::OpCode::CALL:
        case ir::OpCode::CALL_VARIADIC:
        case ir::OpCode::FMA_F32:
        case ir::OpCode::SELECT:
        case ir::OpCode::BOOL_SELECT:
            return {true, true, true};

        // Reads 0 virtual registers
        case ir::OpCode::CONST:
        case ir::OpCode::IMMEDIATE:
        case ir::OpCode::STRING_CONST:
        case ir::OpCode::BRANCH:
        case ir::OpCode::GOTO:
        case ir::OpCode::NOP:
        case ir::OpCode::LABEL:
        case ir::OpCode::PHI_NODE:
        case ir::OpCode::SYSCALL:
            return {false, false, false};

        default:
            if (dgtl::isa::lowering::runtime_contract::has_runtime_symbol(opcode)) {
                return {true, true, true};
            }
            return {false, false, false};
        }
    }

public:
    template <typename LabelMapType>
    void compute(const ir::IRStream& stream, const LabelMapType& label_map) {
        intervals_.clear();
        intervals_.reserve(stream.size() * 2);

        // Track only IDs produced by this stream so literals/labels are not treated as registers
        std::unordered_map<uint32_t, bool> produced_ids;
        produced_ids.reserve(stream.size());
        for (size_t i = 0; i < stream.size(); ++i) {
            const auto& node = stream.get_node(i);
            if (node.result_id != ir::INVALID_SLOT) {
                produced_ids.emplace(node.result_id, true);
            }
        }

        for (size_t i = 0; i < stream.size(); ++i) {
            const auto& node = stream.get_node(i);
            
            auto track_use = [&](uint32_t virtual_id) {
                if (virtual_id == ir::INVALID_SLOT) {
                    return;
                }
                if (produced_ids.find(virtual_id) == produced_ids.end()) {
                    return;
                }
                if (virtual_id >= intervals_.size()) {
                    intervals_.resize(static_cast<size_t>(virtual_id) + 1);
                }
                auto& interval = intervals_[virtual_id];
                if (!interval.active) {
                    interval.active = true;
                    interval.start_node = i;
                }
                if (i > interval.end_node) {
                    interval.end_node = i;
                }
            };

            if (node.result_id != ir::INVALID_SLOT) {
                if (node.result_id >= intervals_.size()) {
                    intervals_.resize(static_cast<size_t>(node.result_id) + 1);
                }
                auto& interval = intervals_[node.result_id];
                if (!interval.active) {
                    interval.active = true;
                    interval.start_node = i;
                }
                if (i > interval.end_node) {
                    interval.end_node = i;
                }
            }

            const OperandUseMask uses = operand_uses(node.opcode);
            if (uses.use_a) track_use(node.op_a);
            if (uses.use_b) track_use(node.op_b);
            if (uses.use_c) track_use(node.op_c);
        }

        // --- Backedge Resolution ---
        // Extends live intervals across loop scopes to protect loop-carried induction variables
        for (size_t i = 0; i < stream.size(); ++i) {
            const auto& node = stream.get_node(i);
            if (node.opcode == ir::OpCode::BRANCH_COND || 
                node.opcode == ir::OpCode::BRANCH_LIKELY ||
                node.opcode == ir::OpCode::BRANCH_UNLIKELY ||
                node.opcode == ir::OpCode::BRANCH_COMPARE_FUSED ||
                node.opcode == ir::OpCode::GOTO) {

                uint32_t target_label = ir::INVALID_SLOT;
                if (node.opcode == ir::OpCode::BRANCH_COMPARE_FUSED) {
                    target_label = node.op_c;
                } else if (node.opcode == ir::OpCode::BRANCH_COND ||
                           node.opcode == ir::OpCode::BRANCH_LIKELY ||
                           node.opcode == ir::OpCode::BRANCH_UNLIKELY) {
                    target_label = (node.op_b != ir::INVALID_SLOT && node.op_b != 0u) ? node.op_b : node.result_id;
                } else {
                    target_label = (node.op_a != ir::INVALID_SLOT && node.op_a != 0u) ? node.op_a : node.result_id;
                }

                size_t loop_start = 0;
                if (!lookup_label_index(label_map, target_label, loop_start)) {
                    continue;
                }
                if (loop_start >= i) {
                    continue; // Forward jump
                }

                const size_t loop_end = i;
                for (auto& interval : intervals_) {
                    if (interval.active && interval.start_node <= loop_end && interval.end_node >= loop_start) {
                        if (interval.end_node < loop_end) {
                            interval.end_node = loop_end;
                        }
                    }
                }
            }
        }
    }

    [[nodiscard]] inline bool is_dead(uint32_t virtual_id, size_t current_node_idx) const noexcept {
        if (virtual_id == ir::INVALID_SLOT || static_cast<size_t>(virtual_id) >= intervals_.size()) {
            return true;
        }
        const auto& interval = intervals_[virtual_id];
        if (!interval.active) {
            return true;
        }
        return current_node_idx >= interval.end_node;
    }
};

} // namespace dgtl::isa::regalloc