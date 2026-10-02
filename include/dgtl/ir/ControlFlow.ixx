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
#include <cstdint>
#include <span>
#include <vector>

export module dgtl.ir_control_flow;

// FIXED CYCLIC LOOP: Import local leaf sibling dependencies natively via short-form shortcuts
// Import standalone primitive modules
import dgtl.ir_node;
import dgtl.ir_enums;

import dgtl.ir.stream;



export namespace dgtl::ir {

struct DGTLPhiInput {
    uint32_t predecessor_block = dgtl::ir::INVALID_SLOT;
    uint32_t slot_id = dgtl::ir::INVALID_SLOT;
};

struct DGTLBasicBlock {
    uint32_t id = 0;
    size_t begin_index = 0;
    size_t end_index = 0;
    bool sealed = false;
    std::vector<uint32_t> predecessors;
    std::vector<uint32_t> successors;
};

class DGTLControlFlowGraph {
public:
    /// Rebuild this CFG from IRStream's canonical block/edge view.
    /// This keeps frontend/core CFG structures linked to the IR-layer CFG
    /// without re-parsing branch opcodes in multiple places.
    [[nodiscard]] bool rebuild_from_ir_stream(const IRStream& stream) {
        blocks_.clear();
        next_block_id_ = 0;

        const auto& ir_blocks = stream.get_basic_blocks();
        const auto& ir_edges  = stream.get_cfg_edges();
        if (ir_blocks.empty()) [[unlikely]] {
            return true;
        }

        blocks_.reserve(ir_blocks.size());
        uint32_t max_id = 0;
        for (const auto& b : ir_blocks) {
            DGTLBasicBlock blk;
            blk.id = b.id;
            blk.begin_index = b.begin;
            blk.end_index = b.end;
            blk.sealed = true;
            blocks_.push_back(blk);
            if (b.id > max_id) max_id = b.id;
        }
        next_block_id_ = max_id + 1u;

        const bool edges_ok = std::all_of(ir_edges.begin(), ir_edges.end(), [this](const auto& e) {
            return add_edge(e.from, e.to);
        });
        if (!edges_ok) [[unlikely]] {
            return false;
        }

        return validate();
    }

    [[nodiscard]] uint32_t create_block(size_t begin_index = 0) {
        const uint32_t id = next_block_id_++;
        blocks_.push_back(DGTLBasicBlock{id, begin_index, begin_index, false, {}, {}});
        return id;
    }

    [[nodiscard]] DGTLBasicBlock* find_block(uint32_t block_id) noexcept {
        if (block_id < blocks_.size() && blocks_[block_id].id == block_id) [[likely]] {
            return &blocks_[block_id];
        }
        auto it = std::find_if(blocks_.begin(), blocks_.end(), [block_id](const DGTLBasicBlock& block) noexcept {
            return block.id == block_id;
        });
        return it == blocks_.end() ? nullptr : &(*it);
    }

    [[nodiscard]] const DGTLBasicBlock* find_block(uint32_t block_id) const noexcept {
        if (block_id < blocks_.size() && blocks_[block_id].id == block_id) [[likely]] {
            return &blocks_[block_id];
        }
        auto it = std::find_if(blocks_.begin(), blocks_.end(), [block_id](const DGTLBasicBlock& block) noexcept {
            return block.id == block_id;
        });
        return it == blocks_.end() ? nullptr : &(*it);
    }

    [[nodiscard]] bool add_edge(uint32_t from_block, uint32_t to_block) {
        DGTLBasicBlock* from = find_block(from_block);
        DGTLBasicBlock* to = find_block(to_block);
        if (!from || !to) [[unlikely]] {
            return false;
        }

        if (std::find(from->successors.begin(), from->successors.end(), to_block) == from->successors.end()) {
            from->successors.push_back(to_block);
        }
        if (std::find(to->predecessors.begin(), to->predecessors.end(), from_block) == to->predecessors.end()) {
            to->predecessors.push_back(from_block);
        }
        return true;
    }

    [[nodiscard]] bool seal_block(uint32_t block_id, size_t end_index) {
        DGTLBasicBlock* block = find_block(block_id);
        if (!block) [[unlikely]] {
            return false;
        }
        block->end_index = end_index;
        block->sealed = true;
        return true;
    }

    [[nodiscard]] uint32_t insert_phi(IRStream& stream,
                                      uint32_t merge_block,
                                      uint32_t result_slot,
                                      std::span<const DGTLPhiInput> inputs,
                                      uint16_t flags = dgtl::ir::Flags::NONE) {
        if (inputs.empty()) [[unlikely]] {
            return dgtl::ir::INVALID_SLOT;
        }
        if (result_slot == dgtl::ir::INVALID_SLOT) [[unlikely]] {
            return dgtl::ir::INVALID_SLOT;
        }

        const DGTLBasicBlock* merge = find_block(merge_block);
        if (!merge) [[unlikely]] {
            return dgtl::ir::INVALID_SLOT;
        }

        for (const auto& in : inputs) {
            if (in.predecessor_block == dgtl::ir::INVALID_SLOT ||
                in.slot_id == dgtl::ir::INVALID_SLOT) [[unlikely]] {
                return dgtl::ir::INVALID_SLOT;
            }
            if (std::find(merge->predecessors.begin(),
                          merge->predecessors.end(),
                          in.predecessor_block) == merge->predecessors.end()) [[unlikely]] {
                return dgtl::ir::INVALID_SLOT;
            }
        }

        // Preserve high-arity joins losslessly through PHI_MULTI sidecars.
        if (inputs.size() > 3) {
            return insert_phi_multi(stream, merge_block, result_slot, inputs, flags);
        }

        std::vector<uint32_t> work;
        work.reserve(inputs.size());
        std::transform(inputs.begin(), inputs.end(), std::back_inserter(work),
                       [](const auto& in) { return in.slot_id; });

        uint32_t next_temp_slot = std::max(stream.get_max_id(), result_slot);
        if (next_temp_slot == dgtl::ir::INVALID_SLOT) [[unlikely]] {
            return dgtl::ir::INVALID_SLOT;
        }
        ++next_temp_slot;

        // IRNode stores up to 3 incoming slots; fold larger joins into a PHI tree.
        while (work.size() > 3) {
            if (next_temp_slot == dgtl::ir::INVALID_SLOT) [[unlikely]] {
                return dgtl::ir::INVALID_SLOT;
            }

            const uint32_t temp_slot = next_temp_slot++;
            stream.push_node(dgtl::ir::OpCode::PHI_NODE,
                             flags,
                             temp_slot,
                             work[0],
                             work[1],
                             work[2],
                             0);

            work.erase(work.begin(), work.begin() + 3);
            work.insert(work.begin(), temp_slot);
        }

        const uint32_t op_a = work.size() > 0 ? work[0] : dgtl::ir::INVALID_SLOT;
        const uint32_t op_b = work.size() > 1 ? work[1] : dgtl::ir::INVALID_SLOT;
        const uint32_t op_c = work.size() > 2 ? work[2] : dgtl::ir::INVALID_SLOT;

        stream.push_node(dgtl::ir::OpCode::PHI_NODE,
                         flags,
                         result_slot,
                         op_a,
                         op_b,
                         op_c,
                         0);
        if (stream.size() > 0) [[likely]] {
            const size_t node_idx = stream.size() - 1;
            std::vector<uint32_t> predecessor_ids;
            predecessor_ids.reserve(inputs.size());
            std::transform(inputs.begin(), inputs.end(), std::back_inserter(predecessor_ids),
                           [](const auto& in) { return in.predecessor_block; });
            stream.set_result_ids(node_idx, predecessor_ids);
            stream.set_node_effects(node_idx, merge_block);
        }
        return result_slot;
    }

    [[nodiscard]] uint32_t insert_phi_multi(IRStream& stream,
                                             uint32_t merge_block,
                                             uint32_t result_slot,
                                             std::span<const DGTLPhiInput> inputs,
                                             uint16_t flags = dgtl::ir::Flags::NONE) {
        if (inputs.empty() || result_slot == dgtl::ir::INVALID_SLOT) [[unlikely]] {
            return dgtl::ir::INVALID_SLOT;
        }

        const DGTLBasicBlock* merge = find_block(merge_block);
        if (!merge) [[unlikely]] {
            return dgtl::ir::INVALID_SLOT;
        }

        for (const auto& in : inputs) {
            if (in.predecessor_block == dgtl::ir::INVALID_SLOT ||
                in.slot_id == dgtl::ir::INVALID_SLOT) [[unlikely]] {
                return dgtl::ir::INVALID_SLOT;
            }
            if (std::find(merge->predecessors.begin(),
                          merge->predecessors.end(),
                          in.predecessor_block) == merge->predecessors.end()) [[unlikely]] {
                return dgtl::ir::INVALID_SLOT;
            }
        }

        const uint32_t op_a = inputs.size() > 0 ? inputs[0].slot_id : dgtl::ir::INVALID_SLOT;
        const uint32_t op_b = inputs.size() > 1 ? inputs[1].slot_id : dgtl::ir::INVALID_SLOT;
        const uint32_t op_c = inputs.size() > 2 ? inputs[2].slot_id : dgtl::ir::INVALID_SLOT;

        stream.push_node(dgtl::ir::OpCode::PHI_MULTI,
                         flags,
                         result_slot,
                         op_a,
                         op_b,
                         op_c,
                         0);

        if (stream.size() == 0) [[unlikely]] {
            return dgtl::ir::INVALID_SLOT;
        }

        const size_t node_idx = stream.size() - 1;
        if (inputs.size() > 3) {
            std::vector<uint32_t> overflow_values;
            overflow_values.reserve(inputs.size() - 3);
            for (size_t i = 3; i < inputs.size(); ++i) {
                overflow_values.push_back(inputs[i].slot_id);
            }
            stream.set_operand_overflow(node_idx, overflow_values);
        }

        std::vector<uint32_t> predecessor_ids;
        predecessor_ids.reserve(inputs.size());
        std::transform(inputs.begin(), inputs.end(), std::back_inserter(predecessor_ids),
                       [](const auto& in) { return in.predecessor_block; });
        stream.set_result_ids(node_idx, predecessor_ids);
        stream.set_node_effects(node_idx, merge_block);
        return result_slot;
    }

    [[nodiscard]] static std::vector<DGTLPhiInput> decode_phi_inputs(const IRStream& stream,
                                                                    size_t node_idx) {
        std::vector<DGTLPhiInput> decoded;
        if (node_idx >= stream.size()) [[unlikely]] {
            return decoded;
        }

        const IRNode node = stream.get_node(node_idx);
        if (node.opcode != dgtl::ir::OpCode::PHI_NODE && node.opcode != dgtl::ir::OpCode::PHI_MULTI) {
            return decoded;
        }

        std::vector<uint32_t> values;
        values.reserve(3);
        if (node.op_a != dgtl::ir::INVALID_SLOT) values.push_back(node.op_a);
        if (node.op_b != dgtl::ir::INVALID_SLOT) values.push_back(node.op_b);
        if (node.op_c != dgtl::ir::INVALID_SLOT) values.push_back(node.op_c);

        const auto overflow = stream.get_operand_overflow(node_idx);
        values.insert(values.end(), overflow.begin(), overflow.end());

        const auto predecessor_ids = stream.get_result_ids(node_idx);
        const size_t count = std::min(values.size(), predecessor_ids.size());
        decoded.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            decoded.push_back(DGTLPhiInput{predecessor_ids[i], values[i]});
        }
        return decoded;
    }

    [[nodiscard]] bool validate() const noexcept {
        return std::all_of(blocks_.begin(), blocks_.end(), [this](const auto& block) {
            const bool succ_ok = std::all_of(block.successors.begin(), block.successors.end(), [this](uint32_t s) {
                return find_block(s) != nullptr;
            });
            const bool pred_ok = std::all_of(block.predecessors.begin(), block.predecessors.end(), [this](uint32_t p) {
                return find_block(p) != nullptr;
            });
            return succ_ok && pred_ok;
        });
    }

    [[nodiscard]] const std::vector<DGTLBasicBlock>& blocks() const noexcept {
        return blocks_;
    }

private:
    std::vector<DGTLBasicBlock> blocks_;
    uint32_t next_block_id_ = 0;
};

} // namespace dgtl::ir


