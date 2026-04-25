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
        (void)merge_block;
        if (inputs.empty()) {
            return dgtl::ir::INVALID_SLOT;
        }

        const uint32_t op_a = inputs.size() > 0 ? inputs[0].slot_id : dgtl::ir::INVALID_SLOT;
        const uint32_t op_b = inputs.size() > 1 ? inputs[1].slot_id : dgtl::ir::INVALID_SLOT;
        const uint32_t op_c = inputs.size() > 2 ? inputs[2].slot_id : dgtl::ir::INVALID_SLOT;

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