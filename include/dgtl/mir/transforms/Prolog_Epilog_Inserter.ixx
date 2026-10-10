/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Component:   Prologue/Epilogue Inserter (Real MIR Instructions Insertion)
 * Standards:   C++26 / Clang 18+ / MSVC 19.40+ / GCC 14+
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

module;

#include <vector>

export module dgtl.mir.prolog_epilog_inserter;

import dgtl.mir.machine_ir;

export namespace dgtl::mir {

using namespace dgtl::machine;

class PrologEpilogInserter {
public:
    static bool run(MachineFunction& function, const TargetFrameInfo& frame_info) {
        if (function.blocks().empty()) return true;

        // 1. Emit target prologue into all entry blocks
        for (std::size_t b_idx = 0; b_idx < function.blocks().size(); ++b_idx) {
            MachineBasicBlock* bb = function.block(static_cast<BlockId>(b_idx));
            if (!bb) continue;
            if (bb->entry || b_idx == 0) {
                frame_info.emit_prologue(function, bb->id);
            }
        }

        // 2. Identify all blocks containing return instructions and emit target epilogue
        for (std::size_t b_idx = 0; b_idx < function.blocks().size(); ++b_idx) {
            MachineBasicBlock* bb = function.block(static_cast<BlockId>(b_idx));
            if (!bb || bb->instructions.empty()) continue;

            const InstrId tail_id = bb->tail_instr;
            const MachineInstruction* tail = function.instruction(tail_id);
            if (tail && tail->is_return()) {
                frame_info.emit_epilogue(function, bb->id);
            }
        }

        return true;
    }
};

} // namespace dgtl::mir
