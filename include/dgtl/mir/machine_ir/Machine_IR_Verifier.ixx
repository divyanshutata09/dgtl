/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Component:   Machine IR Verifier, CFG Consistency & SSA Dominance Validation
 * Standards:   C++26 / Clang 18+ / MSVC 19.40+ / GCC 14+
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

module;

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>

export module dgtl.mir.machine_ir:verifier;

import dgtl.lut_fsm;
import :types;
import :target;
import :function;
import :analysis;

export namespace dgtl::machine {

enum class MachineVerifyError : std::uint8_t {
    None,
    InvalidBlock,
    InvalidInstruction,
    InvalidOperand,
    InvalidRegister,
    MissingTerminator,
    InvalidCFGEdge,
    DuplicateBlock,
    DuplicateInstruction,
    InvalidStackSlot,
    InvalidMemoryOperand,
    InvalidOperandRole,
    MissingSemanticKind,
    MissingTargetOpcode,
    InvalidTargetOpcodeState,
    InvalidPhaseTransition,
    MissingEntryBlock,
    PhaseViolation,
    CFGIntegrityViolation,
    SSADominanceViolation
};

struct MachineVerifyResult {
    MachineVerifyError error = MachineVerifyError::None;
    BlockId block = INVALID_BLOCK_ID;
    InstrId instruction = INVALID_INSTR_ID;

    [[nodiscard]] constexpr bool ok() const noexcept {
        return error == MachineVerifyError::None;
    }
};

[[nodiscard]] constexpr dgtl::utils::BitMatrixLUT<16, 16> build_operand_role_legality_lut() noexcept {
    dgtl::utils::BitMatrixLUT<16, 16> lut{};
    for (std::size_t r = 0; r <= 7; ++r) {
        lut.set(static_cast<std::size_t>(MachineOperandKind::Register), r, true);
    }
    auto set_read_only = [&](MachineOperandKind k) {
        lut.set(static_cast<std::size_t>(k), static_cast<std::size_t>(OperandRole::Use), true);
        lut.set(static_cast<std::size_t>(k), static_cast<std::size_t>(OperandRole::Debug), true);
    };
    set_read_only(MachineOperandKind::Immediate);
    set_read_only(MachineOperandKind::FloatingImmediate);
    set_read_only(MachineOperandKind::BasicBlock);
    set_read_only(MachineOperandKind::Symbol);
    set_read_only(MachineOperandKind::StackSlot);
    set_read_only(MachineOperandKind::ConstantPoolIndex);
    set_read_only(MachineOperandKind::JumpTableIndex);
    set_read_only(MachineOperandKind::RegisterMask);
    set_read_only(MachineOperandKind::Metadata);
    set_read_only(MachineOperandKind::Memory);
    return lut;
}

inline constexpr auto kOperandRoleLegalityLUT = build_operand_role_legality_lut();

inline bool MachineFunction::verify_cfg_integrity(std::string* out_error) const {
    const BlockId root = entry_block();
    if (root != INVALID_BLOCK_ID && root >= blocks().size()) {
        if (out_error) *out_error = "Entry block ID is out of range.";
        return false;
    }
    for (std::size_t i = 0; i < blocks().size(); ++i) {
        const auto* bb_ptr = block(static_cast<BlockId>(i));
        if (!bb_ptr) continue;
        const auto& bb = *bb_ptr;
        if (bb.id != static_cast<BlockId>(i)) {
            if (out_error) *out_error = "Block id does not match vector index at " + std::to_string(i);
            return false;
        }
        for (BlockId succ_id : bb.successors) {
            if (succ_id >= blocks().size()) {
                if (out_error) *out_error = "Successor " + std::to_string(succ_id) + " out of range in block " + std::to_string(bb.id);
                return false;
            }
            const auto* succ_bb = block(succ_id);
            if (!succ_bb || std::find(succ_bb->predecessors.begin(), succ_bb->predecessors.end(), bb.id) == succ_bb->predecessors.end()) {
                if (out_error) *out_error = "CFG asymmetry: block " + std::to_string(bb.id) + " has successor " + std::to_string(succ_id) + " but successor lacks predecessor back-edge.";
                return false;
            }
        }
        for (BlockId pred_id : bb.predecessors) {
            if (pred_id >= blocks().size()) {
                if (out_error) *out_error = "Predecessor " + std::to_string(pred_id) + " out of range in block " + std::to_string(bb.id);
                return false;
            }
            const auto* pred_bb = block(pred_id);
            if (!pred_bb || std::find(pred_bb->successors.begin(), pred_bb->successors.end(), bb.id) == pred_bb->successors.end()) {
                if (out_error) *out_error = "CFG asymmetry: block " + std::to_string(bb.id) + " has predecessor " + std::to_string(pred_id) + " but predecessor lacks successor forward-edge.";
                return false;
            }
        }
        InstrId curr = bb.head_instr;
        InstrId prev = INVALID_INSTR_ID;
        std::size_t counted = 0;
        while (curr != INVALID_INSTR_ID) {
            if (curr >= instructions().size()) {
                if (out_error) *out_error = "Instruction ID " + std::to_string(curr) + " out of range in block " + std::to_string(bb.id);
                return false;
            }
            const auto* inst = instruction(curr);
            if (!inst || inst->parent_block != bb.id) {
                if (out_error) *out_error = "Instruction " + std::to_string(curr) + " parent_block mismatch in block " + std::to_string(bb.id);
                return false;
            }
            if (inst->prev_instr != prev) {
                if (out_error) *out_error = "Instruction chain broken: prev_instr mismatch at instr " + std::to_string(curr);
                return false;
            }
            prev = curr;
            curr = inst->next_instr;
            counted++;
            if (counted > instructions().size()) {
                if (out_error) *out_error = "Cycle detected in instruction intrusive list of block " + std::to_string(bb.id);
                return false;
            }
        }
        if (prev != bb.tail_instr) {
            if (out_error) *out_error = "Tail instruction pointer mismatch in block " + std::to_string(bb.id);
            return false;
        }
        if (counted != bb.instructions.size()) {
            if (out_error) *out_error = "Intrusive chain count " + std::to_string(counted) + " does not match instructions vector size " + std::to_string(bb.instructions.size()) + " in block " + std::to_string(bb.id);
            return false;
        }
    }
    return true;
}

inline bool MachineFunction::verify_ssa_dominance(std::string* out_error) const {
    if (!properties().is_ssa) {
        return true;
    }

    struct DefLocation {
        InstrId instr_id = INVALID_INSTR_ID;
        BlockId block_id = INVALID_BLOCK_ID;
        std::size_t instr_index_in_block = 0;
    };

    std::unordered_map<RegId, DefLocation> vreg_defs;

    for (const auto& bb : blocks()) {
        for (std::size_t idx = 0; idx < bb.instructions.size(); ++idx) {
            const InstrId iid = bb.instructions[idx];
            const auto* inst = instruction(iid);
            if (!inst) continue;

            for (const auto& op : inst->operands) {
                if (op.is_register() && op.is_def() && op.reg.is_virtual()) {
                    const RegId r = op.reg.id;
                    if (vreg_defs.find(r) != vreg_defs.end()) {
                        if (out_error) {
                            *out_error = "SSA violation: virtual register v" + std::to_string(r) +
                                         " defined multiple times (first in instr " +
                                         std::to_string(vreg_defs[r].instr_id) + ", then in instr " +
                                         std::to_string(iid) + ").";
                        }
                        return false;
                    }
                    vreg_defs[r] = DefLocation{iid, bb.id, idx};
                }
            }
        }
    }

    const auto& dom = dominators();

    for (const auto& bb : blocks()) {
        for (std::size_t idx = 0; idx < bb.instructions.size(); ++idx) {
            const InstrId iid = bb.instructions[idx];
            const auto* inst = instruction(iid);
            if (!inst) continue;

            const bool is_phi = inst->is_phi();

            if (is_phi) {
                for (std::size_t op_idx = 1; op_idx + 1 < inst->operands.size(); op_idx += 2) {
                    const auto& block_op = inst->operands[op_idx];
                    const auto& val_op = inst->operands[op_idx + 1];
                    if (block_op.kind == MachineOperandKind::BasicBlock &&
                        val_op.is_register() && val_op.reg.is_virtual()) {
                        const RegId r = val_op.reg.id;
                        auto def_it = vreg_defs.find(r);
                        if (def_it == vreg_defs.end()) {
                            if (out_error) {
                                *out_error = "SSA use without def: virtual register v" + std::to_string(r) +
                                             " used in PHI instruction " + std::to_string(iid) + " has no definition.";
                            }
                            return false;
                        }
                        const BlockId pred_block = block_op.block;
                        const DefLocation& def_loc = def_it->second;
                        if (!dom.dominates(def_loc.block_id, pred_block)) {
                            if (out_error) {
                                *out_error = "SSA dominance violation: definition of v" + std::to_string(r) +
                                             " in block " + std::to_string(def_loc.block_id) +
                                             " does not dominate PHI incoming predecessor block " +
                                             std::to_string(pred_block) + " in block " + std::to_string(bb.id);
                            }
                            return false;
                        }
                    }
                }
            } else {
                for (const auto& op : inst->operands) {
                    if (op.is_register() && op.is_use() && op.reg.is_virtual()) {
                        const RegId r = op.reg.id;
                        auto def_it = vreg_defs.find(r);
                        if (def_it == vreg_defs.end()) {
                            if (out_error) {
                                *out_error = "SSA use without def: virtual register v" + std::to_string(r) +
                                             " used in instruction " + std::to_string(iid) + " has no definition.";
                            }
                            return false;
                        }
                        const DefLocation& def_loc = def_it->second;
                        if (def_loc.block_id == bb.id) {
                            if (def_loc.instr_index_in_block >= idx) {
                                if (out_error) {
                                    *out_error = "SSA intra-block dominance violation: use of v" + std::to_string(r) +
                                                 " at instr " + std::to_string(iid) + " precedes its definition at instr " +
                                                 std::to_string(def_loc.instr_id);
                                }
                                return false;
                            }
                        } else {
                            if (!dom.dominates(def_loc.block_id, bb.id)) {
                                if (out_error) {
                                    *out_error = "SSA dominance violation: definition of v" + std::to_string(r) +
                                                 " in block " + std::to_string(def_loc.block_id) +
                                                 " does not dominate use block " + std::to_string(bb.id) +
                                                 " at instr " + std::to_string(iid);
                                }
                                return false;
                            }
                        }
                    }
                }
            }
        }
    }
    return true;
}

inline BlockBranchTopology MachineFunction::block_branch_topology(BlockId block_id) const noexcept {
    const auto* bb = block(block_id);
    if (!bb) return BlockBranchTopology::Malformed;
    if (bb->instructions.empty() || bb->tail_instr == INVALID_INSTR_ID) {
        return BlockBranchTopology::EmptyOrFallthrough;
    }
    const auto* tail = instruction(bb->tail_instr);
    if (!tail) return BlockBranchTopology::Malformed;

    if (!tail->has_flag(MachineInstrFlag::Terminator)) {
        return BlockBranchTopology::EmptyOrFallthrough;
    }

    if (tail->form == MachineForm::Return || tail->has_flag(MachineInstrFlag::Return)) {
        return BlockBranchTopology::Return;
    }
    if (tail->form == MachineForm::Syscall) {
        return BlockBranchTopology::SyscallOrTrap;
    }
    if (tail->form == MachineForm::Goto) {
        if (tail->prev_instr != INVALID_INSTR_ID) {
            const auto* prev = instruction(tail->prev_instr);
            if (prev && (prev->form == MachineForm::BranchCond || prev->form == MachineForm::BranchCompare)) {
                return BlockBranchTopology::ConditionalAndUnconditionalBranch;
            }
        }
        return BlockBranchTopology::UnconditionalBranch;
    }
    if (tail->form == MachineForm::BranchCond || tail->form == MachineForm::BranchCompare) {
        return BlockBranchTopology::ConditionalBranchWithFallthrough;
    }
    for (const auto& op : tail->operands) {
        if (op.kind == MachineOperandKind::JumpTableIndex) {
            return BlockBranchTopology::IndirectBranch;
        }
    }
    return BlockBranchTopology::UnconditionalBranch;
}

class MachineVerifier {
public:
    [[nodiscard]] static MachineVerifyResult
    verify(const MachineFunction& function) noexcept {
        if (!function.verify_cfg_integrity()) {
            return {MachineVerifyError::CFGIntegrityViolation, INVALID_BLOCK_ID, INVALID_INSTR_ID};
        }
        if (function.properties().is_ssa && !function.verify_ssa_dominance()) {
            return {MachineVerifyError::SSADominanceViolation, INVALID_BLOCK_ID, INVALID_INSTR_ID};
        }

        const auto blocks = function.blocks();
        const auto instructions = function.instructions();

        std::vector<std::uint32_t> instruction_membership(instructions.size(), 0u);

        if (function.is_phase_at_least(MachinePhase::PreRegAlloc)) {
            if (blocks.empty() || function.entry_block() == INVALID_BLOCK_ID ||
                function.entry_block() >= blocks.size() ||
                !blocks[function.entry_block()].entry) {
                return {MachineVerifyError::MissingEntryBlock,
                        INVALID_BLOCK_ID, INVALID_INSTR_ID};
            }
        }

        for (std::size_t i = 0; i < blocks.size(); ++i) {
            if (blocks[i].id != static_cast<BlockId>(i)) {
                return {
                    MachineVerifyError::DuplicateBlock,
                    blocks[i].id,
                    INVALID_INSTR_ID
                };
            }

            if (blocks[i].instr_count != blocks[i].instructions.size()) {
                return {MachineVerifyError::InvalidInstruction,
                        blocks[i].id, INVALID_INSTR_ID};
            }
            if (blocks[i].instructions.empty()) {
                if (blocks[i].head_instr != INVALID_INSTR_ID ||
                    blocks[i].tail_instr != INVALID_INSTR_ID) {
                    return {MachineVerifyError::InvalidInstruction,
                            blocks[i].id, INVALID_INSTR_ID};
                }
            } else if (blocks[i].head_instr == INVALID_INSTR_ID ||
                       blocks[i].tail_instr == INVALID_INSTR_ID) {
                return {MachineVerifyError::InvalidInstruction,
                        blocks[i].id, INVALID_INSTR_ID};
            }

            bool seen_terminator = false;
            for (std::size_t position = 0; position < blocks[i].instructions.size(); ++position) {
                const InstrId id = blocks[i].instructions[position];
                if (id >= instructions.size()) {
                    return {
                        MachineVerifyError::InvalidInstruction,
                        blocks[i].id,
                        id
                    };
                }
                if (++instruction_membership[id] != 1u) {
                    return {
                        MachineVerifyError::DuplicateInstruction,
                        blocks[i].id,
                        id
                    };
                }

                const MachineInstruction& block_instr = instructions[id];

                if (block_instr.parent_block != blocks[i].id) {
                    return {MachineVerifyError::InvalidInstruction, blocks[i].id, id};
                }

                if (block_instr.form == MachineForm::Unhandled) {
                    return {MachineVerifyError::InvalidInstruction, blocks[i].id, id};
                }

                if (requires_semantic_kind(block_instr.form) &&
                    !block_instr.has_semantic_kind()) {
                    return {MachineVerifyError::MissingSemanticKind, blocks[i].id, id};
                }
                if (block_instr.has_semantic_kind() &&
                    !semantic_kind_matches_form(block_instr.form, block_instr.semantic_kind)) {
                    return {MachineVerifyError::InvalidInstruction, blocks[i].id, id};
                }

                const bool emission_phase =
                    function.is_phase_at_least(MachinePhase::FrameLowered);
                if (emission_phase && !block_instr.has_target_opcode()) {
                    return {MachineVerifyError::MissingTargetOpcode, blocks[i].id, id};
                }

                if (seen_terminator) {
                    return {
                        MachineVerifyError::InvalidInstruction,
                        blocks[i].id,
                        id
                    };
                }
                if (block_instr.has_flag(MachineInstrFlag::Terminator)) {
                    seen_terminator = true;
                    if (position + 1u != blocks[i].instructions.size()) {
                        return {
                            MachineVerifyError::InvalidInstruction,
                            blocks[i].id,
                            id
                        };
                    }
                }

                for (const MachineOperand& operand : block_instr.operands) {
                    switch (operand.kind) {
                    case MachineOperandKind::Register:
                        if (!operand.reg.valid() ||
                            operand.reg.reg_class == RegisterClassKind::Invalid) {
                            return {MachineVerifyError::InvalidRegister, blocks[i].id, id};
                        }
                        break;
                    case MachineOperandKind::BasicBlock:
                        if (operand.block == INVALID_BLOCK_ID || operand.block >= blocks.size()) {
                            return {MachineVerifyError::InvalidOperand, blocks[i].id, id};
                        }
                        break;
                    case MachineOperandKind::StackSlot:
                        if (operand.stack_slot == INVALID_STACK_SLOT ||
                            operand.stack_slot >= function.frame_info().slots.size()) {
                            return {MachineVerifyError::InvalidStackSlot, blocks[i].id, id};
                        }
                        break;
                    case MachineOperandKind::ConstantPoolIndex:
                        if (operand.constant_pool_index >= function.constant_pool().entries().size()) {
                            return {MachineVerifyError::InvalidOperand, blocks[i].id, id};
                        }
                        break;
                    case MachineOperandKind::JumpTableIndex:
                        if (operand.jump_table_index >= function.jump_table_info().entries().size()) {
                            return {MachineVerifyError::InvalidOperand, blocks[i].id, id};
                        }
                        break;
                    case MachineOperandKind::Memory:
                        if (operand.memory.scale != 1 && operand.memory.scale != 2 &&
                            operand.memory.scale != 4 && operand.memory.scale != 8) {
                            return {MachineVerifyError::InvalidMemoryOperand, blocks[i].id, id};
                        }
                        if (operand.memory.alignment == 0) {
                            return {MachineVerifyError::InvalidMemoryOperand, blocks[i].id, id};
                        }
                        if (!operand.memory.load && !operand.memory.store &&
                            (block_instr.has_flag(MachineInstrFlag::Load) ||
                             block_instr.has_flag(MachineInstrFlag::Store))) {
                            return {MachineVerifyError::InvalidMemoryOperand, blocks[i].id, id};
                        }
                        break;
                    case MachineOperandKind::Invalid:
                        return {MachineVerifyError::InvalidOperand, blocks[i].id, id};
                    default:
                        break;
                    }

                    if (!kOperandRoleLegalityLUT.get(static_cast<std::size_t>(operand.kind),
                                                     static_cast<std::size_t>(operand.role))) {
                        return {MachineVerifyError::InvalidOperandRole, blocks[i].id, id};
                    }
                }
            }

            if (function.is_phase_at_least(MachinePhase::PreRegAlloc) &&
                !blocks[i].instructions.empty() && !seen_terminator) {
                return {MachineVerifyError::MissingTerminator,
                        blocks[i].id, INVALID_INSTR_ID};
            }

            for (BlockId successor : blocks[i].successors) {
                if (successor >= blocks.size()) {
                    return {
                        MachineVerifyError::InvalidCFGEdge,
                        blocks[i].id,
                        INVALID_INSTR_ID
                    };
                }
            }

            for (BlockId predecessor : blocks[i].predecessors) {
                if (predecessor >= blocks.size()) {
                    return {
                        MachineVerifyError::InvalidCFGEdge,
                        blocks[i].id,
                        INVALID_INSTR_ID
                    };
                }
            }
        }

        for (const MachineBasicBlock& block : blocks) {
            InstrId cursor = block.head_instr;
            InstrId previous = INVALID_INSTR_ID;
            std::size_t chain_count = 0;
            while (cursor != INVALID_INSTR_ID) {
                if (cursor >= instructions.size() || chain_count > block.instructions.size()) {
                    return {MachineVerifyError::InvalidInstruction,
                            block.id, cursor};
                }
                const MachineInstruction& chained = instructions[cursor];
                if (chained.parent_block != block.id ||
                    chained.prev_instr != previous) {
                    return {MachineVerifyError::InvalidInstruction,
                            block.id, cursor};
                }
                if (std::find(block.instructions.begin(),
                              block.instructions.end(), cursor) == block.instructions.end()) {
                    return {MachineVerifyError::DuplicateInstruction,
                            block.id, cursor};
                }
                previous = cursor;
                cursor = chained.next_instr;
                ++chain_count;
            }
            if (chain_count != block.instructions.size() ||
                previous != block.tail_instr) {
                return {MachineVerifyError::InvalidInstruction,
                        block.id, INVALID_INSTR_ID};
            }
            if (block.tail_instr != INVALID_INSTR_ID &&
                instructions[block.tail_instr].next_instr != INVALID_INSTR_ID) {
                return {MachineVerifyError::InvalidInstruction,
                        block.id, block.tail_instr};
            }

            for (BlockId successor : block.successors) {
                const MachineBasicBlock& target = blocks[successor];
                if (std::find(target.predecessors.begin(),
                              target.predecessors.end(),
                              block.id) == target.predecessors.end()) {
                    return {
                        MachineVerifyError::InvalidCFGEdge,
                        block.id,
                        INVALID_INSTR_ID
                    };
                }
            }
            for (BlockId predecessor : block.predecessors) {
                const MachineBasicBlock& source = blocks[predecessor];
                if (std::find(source.successors.begin(),
                              source.successors.end(),
                              block.id) == source.successors.end()) {
                    return {
                        MachineVerifyError::InvalidCFGEdge,
                        block.id,
                        INVALID_INSTR_ID
                    };
                }
            }
        }

        for (std::size_t i = 0; i < instruction_membership.size(); ++i) {
            if (instruction_membership[i] != 1u) {
                return {
                    MachineVerifyError::DuplicateInstruction,
                    INVALID_BLOCK_ID,
                    static_cast<InstrId>(i)
                };
            }
        }

        for (const MachineInstruction& instr : instructions) {
            if (instr.id == INVALID_INSTR_ID ||
                instr.form == MachineForm::Unhandled) {
                return {
                    MachineVerifyError::InvalidInstruction,
                    INVALID_BLOCK_ID,
                    instr.id
                };
            }

            const bool before_target_lowering =
                static_cast<std::uint8_t>(function.phase()) <
                static_cast<std::uint8_t>(MachinePhase::PreRegAlloc);
            if (before_target_lowering) {
                if (is_target_opcode(instr.opcode)) {
                    return {
                        MachineVerifyError::PhaseViolation,
                        INVALID_BLOCK_ID,
                        instr.id
                    };
                }
            } else {
                if (!instr.has_target_opcode()) {
                    return {
                        MachineVerifyError::MissingTargetOpcode,
                        INVALID_BLOCK_ID,
                        instr.id
                    };
                }
            }

            for (const MachineOperand& operand : instr.operands) {
                if (operand.kind == MachineOperandKind::Register &&
                    !operand.reg.valid()) {
                    return {
                        MachineVerifyError::InvalidRegister,
                        INVALID_BLOCK_ID,
                        instr.id
                    };
                }

                if (operand.kind == MachineOperandKind::BasicBlock &&
                    operand.block >= blocks.size()) {
                    return {
                        MachineVerifyError::InvalidBlock,
                        operand.block,
                        instr.id
                    };
                }

                if (operand.kind == MachineOperandKind::StackSlot &&
                    operand.stack_slot >= function.frame_info().slots.size()) {
                    return {
                        MachineVerifyError::InvalidStackSlot,
                        INVALID_BLOCK_ID,
                        instr.id
                    };
                }
            }
        }

        for (const MachineStackSlot& slot : function.frame_info().slots) {
            if (slot.id == INVALID_STACK_SLOT) {
                return {
                    MachineVerifyError::InvalidStackSlot,
                    INVALID_BLOCK_ID,
                    INVALID_INSTR_ID
                };
            }
            if (slot.alignment == 0u || slot.size_bytes == 0u) {
                return {
                    MachineVerifyError::InvalidStackSlot,
                    INVALID_BLOCK_ID,
                    INVALID_INSTR_ID
                };
            }
        }

        if (static_cast<std::uint8_t>(function.phase()) >=
            static_cast<std::uint8_t>(MachinePhase::PostRegAlloc)) {
            if (function.has_virtual_registers() ||
                !function.properties().registers_allocated) {
                return {
                    MachineVerifyError::InvalidRegister,
                    INVALID_BLOCK_ID,
                    INVALID_INSTR_ID
                };
            }
        }

        if (static_cast<std::uint8_t>(function.phase()) >=
            static_cast<std::uint8_t>(MachinePhase::FrameLowered)) {
            const auto& frame = function.frame_info();

            if (frame.stack_alignment == 0u) {
                return {MachineVerifyError::InvalidStackSlot,
                        INVALID_BLOCK_ID, INVALID_INSTR_ID};
            }

            for (const auto& slot : frame.slots) {
                if (!slot.fixed_offset || slot.alignment == 0u ||
                    slot.size_bytes == 0u) {
                    return {MachineVerifyError::InvalidStackSlot,
                            INVALID_BLOCK_ID, INVALID_INSTR_ID};
                }
                const auto alignment = static_cast<std::int64_t>(slot.alignment);
                if (slot.offset % alignment != 0) {
                    return {MachineVerifyError::InvalidStackSlot,
                            INVALID_BLOCK_ID, INVALID_INSTR_ID};
                }
            }
        }

        if (function.is_phase_at_least(MachinePhase::EmissionReady)) {
            if (!function.has_target_opcodes() || function.has_virtual_registers()) {
                return {MachineVerifyError::InvalidTargetOpcodeState,
                        INVALID_BLOCK_ID, INVALID_INSTR_ID};
            }
            if (!function.properties().frame_lowered ||
                !function.properties().abi_lowered ||
                !function.properties().registers_allocated) {
                return {MachineVerifyError::PhaseViolation,
                        INVALID_BLOCK_ID, INVALID_INSTR_ID};
            }
        }

        return {};
    }
};

} // namespace dgtl::machine