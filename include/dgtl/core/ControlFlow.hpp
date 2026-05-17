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

#include <algorithm>
#include <cstdint>
#include <span>
#include <vector>

#include "dgtl/ir/base/IR_Stream.hpp"

namespace dgtl::core {

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
    [[nodiscard]] bool rebuild_from_ir_stream(const ir::IRStream& stream) {
        blocks_.clear();
        next_block_id_ = 0;

        const auto& ir_blocks = stream.get_basic_blocks();
        const auto& ir_edges  = stream.get_cfg_edges();
        if (ir_blocks.empty()) {
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

        for (const auto& e : ir_edges) {
            if (!add_edge(e.from, e.to)) {
                return false;
            }
        }

        return validate();
    }

    [[nodiscard]] uint32_t create_block(size_t begin_index = 0) {
        const uint32_t id = next_block_id_++;
        blocks_.push_back(DGTLBasicBlock{id, begin_index, begin_index, false, {}, {}});
        return id;
    }

    [[nodiscard]] DGTLBasicBlock* find_block(uint32_t block_id) noexcept {
        auto it = std::find_if(blocks_.begin(), blocks_.end(), [block_id](const DGTLBasicBlock& block) {
            return block.id == block_id;
        });
        return it == blocks_.end() ? nullptr : &(*it);
    }

    [[nodiscard]] const DGTLBasicBlock* find_block(uint32_t block_id) const noexcept {
        auto it = std::find_if(blocks_.begin(), blocks_.end(), [block_id](const DGTLBasicBlock& block) {
            return block.id == block_id;
        });
        return it == blocks_.end() ? nullptr : &(*it);
    }

    [[nodiscard]] bool add_edge(uint32_t from_block, uint32_t to_block) {
        DGTLBasicBlock* from = find_block(from_block);
        DGTLBasicBlock* to = find_block(to_block);
        if (!from || !to) {
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
        if (!block) {
            return false;
        }
        block->end_index = end_index;
        block->sealed = true;
        return true;
    }

    [[nodiscard]] uint32_t insert_phi(ir::IRStream& stream,
                                      uint32_t merge_block,
                                      uint32_t result_slot,
                                      std::span<const DGTLPhiInput> inputs,
                                      uint16_t flags = dgtl::ir::Flags::NONE) {
        if (inputs.empty()) {
            return dgtl::ir::INVALID_SLOT;
        }
        if (result_slot == dgtl::ir::INVALID_SLOT) {
            return dgtl::ir::INVALID_SLOT;
        }

        const DGTLBasicBlock* merge = find_block(merge_block);
        if (!merge) {
            return dgtl::ir::INVALID_SLOT;
        }

        for (const auto& in : inputs) {
            if (in.predecessor_block == dgtl::ir::INVALID_SLOT ||
                in.slot_id == dgtl::ir::INVALID_SLOT) {
                return dgtl::ir::INVALID_SLOT;
            }
            if (std::find(merge->predecessors.begin(),
                          merge->predecessors.end(),
                          in.predecessor_block) == merge->predecessors.end()) {
                return dgtl::ir::INVALID_SLOT;
            }
        }

        std::vector<uint32_t> work;
        work.reserve(inputs.size());
        for (const auto& in : inputs) {
            work.push_back(in.slot_id);
        }

        uint32_t next_temp_slot = std::max(stream.get_max_id(), result_slot);
        if (next_temp_slot == dgtl::ir::INVALID_SLOT) {
            return dgtl::ir::INVALID_SLOT;
        }
        ++next_temp_slot;

        // IRNode stores up to 3 incoming slots; fold larger joins into a PHI tree.
        while (work.size() > 3) {
            if (next_temp_slot == dgtl::ir::INVALID_SLOT) {
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
        return result_slot;
    }

    [[nodiscard]] bool validate() const noexcept {
        for (const auto& block : blocks_) {
            for (uint32_t successor : block.successors) {
                if (!find_block(successor)) {
                    return false;
                }
            }
            for (uint32_t predecessor : block.predecessors) {
                if (!find_block(predecessor)) {
                    return false;
                }
            }
        }
        return true;
    }

    [[nodiscard]] const std::vector<DGTLBasicBlock>& blocks() const noexcept {
        return blocks_;
    }

private:
    std::vector<DGTLBasicBlock> blocks_;
    uint32_t next_block_id_ = 0;
};

} // namespace dgtl::core