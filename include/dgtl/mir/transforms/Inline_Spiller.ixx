/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Component:   Inline Spiller (Pure MIR Stack Spilling via Target Hooks)
 * Standards:   C++26 / Clang 18+ / MSVC 19.40+ / GCC 14+
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

module;

#include <algorithm>
#include <cstdint>
#include <vector>

export module dgtl.mir.inline_spiller;

import dgtl.mir.machine_ir;

export namespace dgtl::mir {

using namespace dgtl::machine;

class InlineSpiller final : public MachineSpiller {
public:
    explicit InlineSpiller(const TargetInstructionInfo* instr_info = nullptr) noexcept
        : instr_info_(instr_info) {}

    [[nodiscard]] SpillResult spill(
        MachineFunction& function,
        const SpillRequest& request) override {
        const std::uint32_t size = std::max<std::uint32_t>(request.size_bytes, 8u);
        const std::uint32_t align = std::max<std::uint32_t>(request.alignment, 8u);

        const StackSlotId slot = function.frame_info().create_slot(
            StackSlotKind::Spill, size, align);

        return SpillResult{
            .spilled = (slot != INVALID_STACK_SLOT),
            .slot = slot,
            .inserted_instructions = {}
        };
    }

    bool rewrite_for_spill(
        MachineFunction& function,
        MachineRegister virtual_reg,
        StackSlotId slot) override {
        if (!virtual_reg.is_virtual() || slot == INVALID_STACK_SLOT) {
            return false;
        }

        // Iterate through all blocks and rewrite definitions and uses
        for (std::size_t b_idx = 0; b_idx < function.blocks().size(); ++b_idx) {
            MachineBasicBlock* block = function.block(static_cast<BlockId>(b_idx));
            if (!block) continue;

            const std::vector<InstrId> original_instructions = block->instructions;

            for (const InstrId inst_id : original_instructions) {
                MachineInstruction* inst = function.instruction(inst_id);
                if (!inst || inst->parent_block == INVALID_BLOCK_ID) continue;

                bool has_def = false;
                bool has_use = false;

                for (const auto& op : inst->operands) {
                    if (op.kind == MachineOperandKind::Register && op.reg == virtual_reg) {
                        if (op.is_def()) has_def = true;
                        if (op.is_use()) has_use = true;
                    } else if (op.kind == MachineOperandKind::Memory) {
                        if (op.memory.base == virtual_reg || op.memory.index == virtual_reg) {
                            has_use = true;
                        }
                    }
                }

                if (!has_def && !has_use) continue;

                if (has_use && has_def) {
                    MachineRegister temp = function.create_virtual_register(virtual_reg.reg_class);

                    // Insert reload before this instruction
                    if (instr_info_ != nullptr) {
                        instr_info_->load_reg_from_stack_slot(function, block->id, inst_id, temp, slot);
                    } else {
                        MachineInstruction& load_inst = function.create_instruction(GM_LOAD_STACK);
                        load_inst.form = MachineForm::LoadStack;
                        load_inst.semantic_kind = MachineSemanticKind::LoadStack;
                        load_inst.add_flag(MachineInstrFlag::Load);
                        load_inst.add_operand(MachineOperand::def(temp));
                        load_inst.add_operand(MachineOperand::stack_ref(slot));
                        load_inst.parent_block = block->id;
                        if (!function.insert_instruction_before(block->id, inst_id, load_inst.id)) {
                            return false;
                        }
                    }

                    // Re-acquire instruction pointer as creating an instruction may reallocate the vector
                    inst = function.instruction(inst_id);
                    if (!inst) return false;

                    // Rewrite all operands (both uses and defs)
                    for (auto& op : inst->operands) {
                        if (op.kind == MachineOperandKind::Register && op.reg == virtual_reg) {
                            op.reg = temp;
                        } else if (op.kind == MachineOperandKind::Memory) {
                            if (op.memory.base == virtual_reg) op.memory.base = temp;
                            if (op.memory.index == virtual_reg) op.memory.index = temp;
                        }
                    }

                    // Insert store after current instruction
                    const InstrId next_cursor = inst->next_instr;
                    if (instr_info_ != nullptr) {
                        instr_info_->store_reg_to_stack_slot(function, block->id, next_cursor, temp, slot);
                    } else {
                        MachineInstruction& store_inst = function.create_instruction(GM_STORE_STACK);
                        store_inst.form = MachineForm::StoreStack;
                        store_inst.semantic_kind = MachineSemanticKind::StoreStack;
                        store_inst.add_flag(MachineInstrFlag::Store);
                        store_inst.add_operand(MachineOperand::stack_ref(slot));
                        store_inst.add_operand(MachineOperand::use(temp));
                        store_inst.parent_block = block->id;
                        if (!function.insert_instruction_before(block->id, next_cursor, store_inst.id)) {
                            return false;
                        }
                    }
                } else if (has_use) {
                    MachineRegister temp_use = function.create_virtual_register(virtual_reg.reg_class);

                    // Insert reload
                    if (instr_info_ != nullptr) {
                        instr_info_->load_reg_from_stack_slot(function, block->id, inst_id, temp_use, slot);
                    } else {
                        MachineInstruction& load_inst = function.create_instruction(GM_LOAD_STACK);
                        load_inst.form = MachineForm::LoadStack;
                        load_inst.semantic_kind = MachineSemanticKind::LoadStack;
                        load_inst.add_flag(MachineInstrFlag::Load);
                        load_inst.add_operand(MachineOperand::def(temp_use));
                        load_inst.add_operand(MachineOperand::stack_ref(slot));
                        load_inst.parent_block = block->id;
                        if (!function.insert_instruction_before(block->id, inst_id, load_inst.id)) {
                            return false;
                        }
                    }

                    // Re-acquire instruction pointer as creating an instruction may reallocate the vector
                    inst = function.instruction(inst_id);
                    if (!inst) return false;

                    // Rewrite operand use
                    for (auto& op : inst->operands) {
                        if (op.kind == MachineOperandKind::Register && op.reg == virtual_reg && op.is_use()) {
                            op.reg = temp_use;
                        } else if (op.kind == MachineOperandKind::Memory) {
                            if (op.memory.base == virtual_reg) op.memory.base = temp_use;
                            if (op.memory.index == virtual_reg) op.memory.index = temp_use;
                        }
                    }
                } else if (has_def) {
                    MachineRegister temp_def = function.create_virtual_register(virtual_reg.reg_class);

                    // Rewrite operand def
                    for (auto& op : inst->operands) {
                        if (op.kind == MachineOperandKind::Register && op.reg == virtual_reg && op.is_def()) {
                            op.reg = temp_def;
                        }
                    }

                    // Insert store after current instruction
                    const InstrId next_cursor = inst->next_instr;
                    if (instr_info_ != nullptr) {
                        instr_info_->store_reg_to_stack_slot(function, block->id, next_cursor, temp_def, slot);
                    } else {
                        MachineInstruction& store_inst = function.create_instruction(GM_STORE_STACK);
                        store_inst.form = MachineForm::StoreStack;
                        store_inst.semantic_kind = MachineSemanticKind::StoreStack;
                        store_inst.add_flag(MachineInstrFlag::Store);
                        store_inst.add_operand(MachineOperand::stack_ref(slot));
                        store_inst.add_operand(MachineOperand::use(temp_def));
                        store_inst.parent_block = block->id;
                        if (!function.insert_instruction_before(block->id, next_cursor, store_inst.id)) {
                            return false;
                        }
                    }
                }
            }
        }

        return true;
    }

private:
    const TargetInstructionInfo* instr_info_;
};

} // namespace dgtl::mir
