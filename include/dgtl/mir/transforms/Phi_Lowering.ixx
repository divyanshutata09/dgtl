/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Component:   Machine IR SSA Deconstruction & Critical-Edge-Aware Phi Lowering
 * Standards:   C++26 / Clang 18+ / MSVC 19.40+ / GCC 14+
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */
module;

#include <cstdlib>
#include <algorithm>
#include <cstdint>
#include <deque>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

export module dgtl.mir.phi_lowering;

import dgtl.mir.machine_ir;

export namespace dgtl::mir::lowering {

using dgtl::machine::BlockId;
using dgtl::machine::InstrId;
using dgtl::machine::RegId;
using dgtl::machine::MachineBasicBlock;
using dgtl::machine::MachineFunction;
using dgtl::machine::MachineInstrFlag;
using dgtl::machine::MachineInstruction;
using dgtl::machine::MachineOperand;
using dgtl::machine::MachineOperandKind;
using dgtl::machine::MachineRegister;
using dgtl::machine::MachineSemanticKind;
using dgtl::machine::INVALID_INSTR_ID;
using dgtl::machine::MachineForm;
using dgtl::machine::GM_COPY;
using dgtl::machine::GM_GOTO;
using dgtl::machine::GM_NOOP;

struct PendingCopy {
    MachineOperand dest{};
    MachineOperand src{};
    InstrId origin_phi = INVALID_INSTR_ID;
};

class MachinePhiLowering {
public:
    /**
     * @brief Deconstructs all SSA Phi nodes across the MachineFunction into parallel copies.
     * Implements critical-edge splitting and topological cycle breaking with scratch temporaries.
     */
    static bool run(MachineFunction& function) {
        if (!function.properties().is_ssa) {
            return true; // Already lowered from SSA
        }

        bool modified = false;

        // Step 1: Global dead-PHI pruning
        std::unordered_set<InstrId> all_phis;
        std::unordered_map<RegId, uint32_t> non_phi_use_counts;

        const std::size_t initial_block_count = function.blocks().size();
        for (std::size_t b_idx = 0; b_idx < initial_block_count; ++b_idx) {
            const MachineBasicBlock* b = function.block(static_cast<BlockId>(b_idx));
            if (!b) continue;

            for (const InstrId inst_id : b->instructions) {
                const auto* inst = function.instruction(inst_id);
                if (!inst) continue;

                if (inst->is_phi()) {
                    all_phis.insert(inst_id);
                } else {
                    for (const auto& op : inst->operands) {
                        if (op.is_register() && op.is_use() && op.reg.valid()) {
                            non_phi_use_counts[op.reg.id]++;
                        }
                    }
                }
            }
        }

        std::unordered_set<InstrId> dead_phis;
        bool changed_dead = true;
        while (changed_dead) {
            changed_dead = false;
            for (const InstrId phi_id : all_phis) {
                if (dead_phis.count(phi_id)) continue;
                const auto* phi_inst = function.instruction(phi_id);
                if (!phi_inst || phi_inst->operands.empty()) {
                    dead_phis.insert(phi_id);
                    changed_dead = true;
                    continue;
                }
                const auto& dest = phi_inst->operands[0];
                if (dest.is_register() && dest.reg.valid()) {
                    uint32_t total_uses = non_phi_use_counts[dest.reg.id];
                    for (const InstrId other_id : all_phis) {
                        if (other_id == phi_id || dead_phis.count(other_id)) continue;
                        const auto* o_inst = function.instruction(other_id);
                        if (!o_inst) continue;
                        for (std::size_t op_i = 1; op_i < o_inst->operands.size(); ++op_i) {
                            const auto& op = o_inst->operands[op_i];
                            if (op.is_register() && op.reg == dest.reg) {
                                total_uses++;
                            }
                        }
                    }
                    if (total_uses == 0) {
                        dead_phis.insert(phi_id);
                        changed_dead = true;
                    }
                }
            }
        }

        // Step 2: Lower live PHIs block by block
        for (std::size_t b_idx = 0; b_idx < initial_block_count; ++b_idx) {
            const BlockId block_id = static_cast<BlockId>(b_idx);
            MachineBasicBlock* bb = function.block(block_id);
            if (!bb) continue;

            std::vector<InstrId> phi_ids;
            for (const InstrId instr_id : bb->instructions) {
                if (all_phis.count(instr_id) && !dead_phis.count(instr_id)) {
                    phi_ids.push_back(instr_id);
                }
            }

            if (phi_ids.empty()) {
                const std::vector<InstrId> block_instr_ids = bb->instructions;
                for (const InstrId instr_id : block_instr_ids) {
                    if (dead_phis.count(instr_id)) {
                        auto* inst = function.instruction(instr_id);
                        if (inst) {
                            inst->form = MachineForm::NoOp;
                            inst->semantic_kind = MachineSemanticKind::Other;
                            inst->opcode = GM_NOOP;
                            inst->operands.clear();
                        }
                        function.remove_instruction(instr_id);
                        modified = true;
                    }
                }
                continue;
            }

            std::unordered_map<BlockId, std::vector<PendingCopy>> edge_copies;

            for (const InstrId phi_id : phi_ids) {
                const auto* phi_inst = function.instruction(phi_id);
                if (!phi_inst || phi_inst->operands.empty()) continue;

                const MachineOperand dest = phi_inst->operands[0];

                for (std::size_t op_idx = 1; op_idx + 1 < phi_inst->operands.size(); op_idx += 2) {
                    const auto& block_op = phi_inst->operands[op_idx];
                    const auto& val_op = phi_inst->operands[op_idx + 1];

                    if (block_op.kind == MachineOperandKind::BasicBlock) {
                        const BlockId pred_id = block_op.block;
                        edge_copies[pred_id].push_back(PendingCopy{dest, val_op, phi_id});
                    }
                }
            }

            for (auto& [pred_id, copies] : edge_copies) {
                MachineBasicBlock* pred_bb = function.block(pred_id);
                if (!pred_bb) continue;

                const bool is_critical = (pred_bb->successors.size() > 1) && (bb->predecessors.size() > 1);

                if (is_critical) {
                    MachineBasicBlock& split_block = function.create_block(false);
                    const BlockId split_id = split_block.id;
                    pred_bb = function.block(pred_id);
                    bb = function.block(block_id);
                    if (!pred_bb || !bb || function.block(split_id) == nullptr) return false;

                    for (auto& succ : pred_bb->successors) {
                        if (succ == block_id) {
                            succ = split_id;
                            break;
                        }
                    }
                    split_block.add_predecessor(pred_id);
                    split_block.add_successor(block_id);

                    for (auto& pred : bb->predecessors) {
                        if (pred == pred_id) {
                            pred = split_id;
                            break;
                        }
                    }

                    MachineInstruction& goto_inst = function.create_instruction();
                    goto_inst.form = MachineForm::Goto;
                    goto_inst.semantic_kind = MachineSemanticKind::Goto;
                    goto_inst.opcode = GM_GOTO;
                    goto_inst.add_flag(MachineInstrFlag::Branch | MachineInstrFlag::Terminator);
                    goto_inst.add_operand(MachineOperand::block_ref(block_id));
                    if (!function.insert_instruction_before(split_id, INVALID_INSTR_ID, goto_inst.id)) {
                        return false;
                    }

                    if (!emit_parallel_copies(function, split_id, copies, false)) return false;
                } else if (pred_bb->successors.size() == 1) {
                    if (!emit_parallel_copies(function, pred_id, copies, false)) return false;
                } else {
                    if (!emit_parallel_copies(function, block_id, copies, true)) return false;
                }

                modified = true;
            }

            std::vector<InstrId> phis_to_remove;
            for (const InstrId phi_id : bb->instructions) {
                if (all_phis.count(phi_id)) {
                    auto* inst = function.instruction(phi_id);
                    if (inst) {
                        inst->form = MachineForm::NoOp;
                        inst->semantic_kind = MachineSemanticKind::Other;
                        inst->opcode = GM_NOOP;
                        inst->operands.clear();
                    }
                    phis_to_remove.push_back(phi_id);
                }
            }
            for (const InstrId phi_id : phis_to_remove) {
                function.remove_instruction(phi_id);
            }
        }

        function.properties().is_ssa = false;
        (void)modified;
        // Contract: the return value signals success (consumed by MachinePipeline),
        // not whether the function was modified. A PHI-free function is a valid,
        // successful deconstruction.
        return true;
    }

private:
    static bool emit_parallel_copies(
        MachineFunction& function,
        BlockId target_block,
        std::vector<PendingCopy>& copies,
        bool at_entry = false) {

        if (copies.empty()) return true;

        MachineBasicBlock* bb = function.block(target_block);
        if (!bb) return false;

        InstrId cursor = INVALID_INSTR_ID;
        if (at_entry) {
            for (const InstrId id : bb->instructions) {
                const auto* inst = function.instruction(id);
                if (!inst) continue;
                if (!inst->is_phi()) {
                    cursor = id;
                    break;
                }
            }
        } else {
            for (const InstrId id : bb->instructions) {
                const auto* inst = function.instruction(id);
                if (inst && inst->has_flag(MachineInstrFlag::Terminator)) {
                    cursor = id;
                    break;
                }
            }
        }

        std::vector<PendingCopy> pending;
        pending.reserve(copies.size());
        for (const auto& copy : copies) {
            if (copy.dest.kind == MachineOperandKind::Register &&
                copy.src.kind == MachineOperandKind::Register &&
                copy.dest.reg == copy.src.reg) {
                continue;
            }
            pending.push_back(copy);
        }

        auto emit_move = [&](const MachineOperand& dst, const MachineOperand& src) {
            MachineInstruction& move_inst = function.create_instruction();
            move_inst.form = MachineForm::Move;
            move_inst.semantic_kind = MachineSemanticKind::Move;
            move_inst.opcode = GM_COPY;
            move_inst.add_operand(dst);
            move_inst.add_operand(src);
            return function.insert_instruction_before(target_block, cursor, move_inst.id);
        };

        while (!pending.empty()) {
            bool progress = false;

            for (auto it = pending.begin(); it != pending.end(); ++it) {
                bool dest_read_by_others = false;
                if (it->dest.kind == MachineOperandKind::Register) {
                    const auto dest_reg = it->dest.reg;
                    for (auto other_it = pending.begin(); other_it != pending.end(); ++other_it) {
                        if (other_it != it &&
                            other_it->src.kind == MachineOperandKind::Register &&
                            other_it->src.reg == dest_reg) {
                            dest_read_by_others = true;
                            break;
                        }
                    }
                }

                if (!dest_read_by_others) {
                    if (!emit_move(it->dest, it->src)) return false;
                    pending.erase(it);
                    progress = true;
                    break;
                }
            }

            if (!progress) {
                auto& cycle_copy = pending.front();
                if (cycle_copy.dest.kind == MachineOperandKind::Register) {
                    const auto victim_reg = cycle_copy.dest.reg;
                    const MachineRegister scratch = function.create_virtual_register(victim_reg.reg_class);

                    if (!emit_move(MachineOperand::def(scratch), MachineOperand::use(victim_reg))) {
                        return false;
                    }

                    for (auto& p : pending) {
                        if (p.src.kind == MachineOperandKind::Register && p.src.reg == victim_reg) {
                            p.src = MachineOperand::use(scratch);
                        }
                    }
                } else {
                    if (!emit_move(cycle_copy.dest, cycle_copy.src)) return false;
                    pending.erase(pending.begin());
                }
            }
        }
        return true;
    }
};

} // namespace dgtl::mir::lowering