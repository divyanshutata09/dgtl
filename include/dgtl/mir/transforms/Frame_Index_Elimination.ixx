/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Component:   Frame Index Elimination (StackSlot -> [RBP + off] Rewriter)
 * Standards:   C++26 / Clang 18+ / MSVC 19.40+ / GCC 14+
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

module;

#include <cstdint>
#include <cstddef>

export module dgtl.mir.frame_index_elimination;

import dgtl.mir.machine_ir;

export namespace dgtl::mir {

using namespace dgtl::machine;

class FrameIndexElimination {
public:
    static bool run(MachineFunction& function, const TargetFrameInfo& frame_info, MachineRegister frame_pointer) {
        const auto& frame = function.frame_info();

        for (std::size_t i = 0; i < function.instructions().size(); ++i) {
            MachineInstruction* inst = function.instruction(static_cast<InstrId>(i));
            if (!inst || inst->parent_block == INVALID_BLOCK_ID) continue;

            for (std::size_t op_idx = 0; op_idx < inst->operands.size(); ++op_idx) {
                MachineOperand& op = inst->operands[op_idx];
                if (op.kind == MachineOperandKind::StackSlot && op.stack_slot < frame.slots.size()) {
                    const auto& slot = frame.slots[op.stack_slot];
                    const std::int64_t offset = slot.offset;

                    // Allow target hook to rewrite first
                    frame_info.eliminate_frame_index(function, *inst, op_idx, offset);

                    // If still a StackSlot, rewrite to standard [fp - offset] memory operand
                    if (inst->operands[op_idx].kind == MachineOperandKind::StackSlot) {
                        MachineMemoryOperand mem{};
                        mem.base = frame_pointer;
                        mem.scale = 1;
                        mem.displacement = -offset;
                        mem.size_bytes = slot.size_bytes;
                        mem.alignment = slot.alignment;
                        if (inst->is_load()) mem.load = true;
                        if (inst->is_store()) mem.store = true;

                        inst->operands[op_idx] = MachineOperand::memory_ref(mem, op.role);
                    }
                }
            }
        }

        return true;
    }
};

} // namespace dgtl::mir
