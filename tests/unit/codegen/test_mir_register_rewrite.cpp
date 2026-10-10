/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

#include <cstddef>
#include <cstdio>
#include <stdexcept>

import dgtl.isa.x64.target;
import dgtl.mir.machine_ir;
import dgtl.mir.phi_lowering;
import dgtl.mir.regalloc.universal;
import dgtl.mir.two_address;
import dgtl.target;

namespace {

using namespace dgtl::machine;

MachineInstruction& append_instruction(MachineFunction& function,
                                       BlockId block,
                                       MachineForm form,
                                       MachineSemanticKind semantic = MachineSemanticKind::Other) {
    MachineInstruction& instruction = function.create_instruction();
    instruction.form = form;
    instruction.semantic_kind = semantic;
    if (!function.insert_instruction_before(block, INVALID_INSTR_ID, instruction.id)) {
        throw std::runtime_error("failed to append MIR test instruction");
    }
    return instruction;
}

void verify_block_instruction_links(const MachineFunction& function) {
    for (const auto& block : function.blocks()) {
        for (std::size_t index = 0; index < block.instructions.size(); ++index) {
            const auto* instruction = function.instruction(block.instructions[index]);
            if (instruction == nullptr || instruction->parent_block != block.id) {
                throw std::runtime_error("MIR block contains an instruction with inconsistent ownership");
            }
            const InstrId expected_previous = index == 0
                ? INVALID_INSTR_ID
                : block.instructions[index - 1u];
            const InstrId expected_next = index + 1u == block.instructions.size()
                ? INVALID_INSTR_ID
                : block.instructions[index + 1u];
            if (instruction->prev_instr != expected_previous || instruction->next_instr != expected_next) {
                throw std::runtime_error("MIR block instruction links disagree with its instruction list");
            }
        }
    }
}

void test_two_address_reallocation() {
    MachineFunction function;
    const BlockId block = function.create_block(true).id;
    const MachineRegister destination = function.create_virtual_register(RegisterClassKind::General);
    const MachineRegister left = function.create_virtual_register(RegisterClassKind::General);
    const MachineRegister right = function.create_virtual_register(RegisterClassKind::General);

    MachineInstruction& add = append_instruction(
        function, block, MachineForm::IntegerBinary64, MachineSemanticKind::IntegerAdd);
    add.add_operand(MachineOperand::def(destination));
    add.add_operand(MachineOperand::use(left));
    add.add_operand(MachineOperand::use(right));
    const InstrId add_id = add.id;

    dgtl::core::TargetMachine target{};
    bool success = false;
    (void)dgtl::mir::lowering::TwoAddressInstructionPass::run(function, target, &success);
    if (!success) throw std::runtime_error("two-address normalization unexpectedly failed");

    const auto* normalized_add = function.instruction(add_id);
    if (normalized_add == nullptr || normalized_add->operands.size() != 3u ||
        normalized_add->operands[1].kind != MachineOperandKind::Register ||
        normalized_add->operands[1].reg != destination || function.blocks()[block].instructions.size() != 2u) {
        throw std::runtime_error("two-address normalization lost or corrupted the source register");
    }

    const auto* copy = function.instruction(function.blocks()[block].instructions.front());
    if (copy == nullptr || copy->form != MachineForm::Move || copy->operands.size() != 2u ||
        copy->operands[0].reg != destination || copy->operands[1].reg != left) {
        throw std::runtime_error("two-address copy operands were corrupted during instruction insertion");
    }

    dgtl::isa::x64::X64TargetInstructionInfo instruction_info;
    for (const InstrId instruction_id : function.blocks()[block].instructions) {
        auto* instruction = function.instruction(instruction_id);
        const MachineOpcode target_opcode = instruction_info.opcode_for(
            instruction->form, instruction->semantic_kind);
        if (target_opcode == INVALID_MACHINE_OPCODE ||
            !instruction->assign_target_opcode_once(target_opcode)) {
            throw std::runtime_error("failed to assign x64 opcode in MIR allocator regression");
        }
    }
    if (!function.mark_target_lowered()) {
        throw std::runtime_error("failed to advance MIR to register allocation");
    }
    dgtl::isa::x64::X64TargetRegisterInfo register_info;
    dgtl::mir::regalloc::UniversalGraphColoringRegAlloc allocator(
        function, register_info, &instruction_info);
    if (!allocator.run() || function.has_virtual_registers() ||
        !function.properties().registers_allocated) {
        throw std::runtime_error("universal register allocation left virtual registers in MIR");
    }
    verify_block_instruction_links(function);
}

void test_instruction_insertion_repairs_stale_owner() {
    MachineFunction function;
    const BlockId block = function.create_block(true).id;
    auto& first = append_instruction(function, block, MachineForm::Move);
    const InstrId first_id = first.id;
    first.parent_block = INVALID_BLOCK_ID;

    (void)append_instruction(function, block, MachineForm::Move);
    const auto* repaired = function.instruction(first_id);
    if (repaired == nullptr || repaired->parent_block != block) {
        throw std::runtime_error("block instruction list did not repair stale owner metadata");
    }
    verify_block_instruction_links(function);
}

void test_critical_edge_phi_reallocation() {
    MachineFunction function;
    const BlockId entry = function.create_block(true).id;
    const BlockId left_block = function.create_block().id;
    const BlockId right_block = function.create_block().id;
    const BlockId merge = function.create_block().id;

    function.connect_blocks(entry, left_block);
    function.connect_blocks(entry, right_block);
    function.connect_blocks(left_block, right_block);
    function.connect_blocks(left_block, merge);
    function.connect_blocks(right_block, merge);

    auto& entry_branch = append_instruction(function, entry, MachineForm::BranchCond);
    entry_branch.add_flag(MachineInstrFlag::Branch | MachineInstrFlag::Terminator);
    auto& left_branch = append_instruction(function, left_block, MachineForm::BranchCond);
    left_branch.add_flag(MachineInstrFlag::Branch | MachineInstrFlag::Terminator);
    auto& right_branch = append_instruction(function, right_block, MachineForm::Goto);
    right_branch.add_flag(MachineInstrFlag::Branch | MachineInstrFlag::Terminator);

    const MachineRegister result = function.create_virtual_register(RegisterClassKind::General);
    const MachineRegister left_value = function.create_virtual_register(RegisterClassKind::General);
    const MachineRegister right_value = function.create_virtual_register(RegisterClassKind::General);
    auto& phi = append_instruction(function, merge, MachineForm::Unhandled);
    phi.add_operand(MachineOperand::def(result));
    phi.add_operand(MachineOperand::block_ref(left_block));
    phi.add_operand(MachineOperand::use(left_value));
    phi.add_operand(MachineOperand::block_ref(right_block));
    phi.add_operand(MachineOperand::use(right_value));
    const InstrId phi_id = phi.id;
    auto& ret = append_instruction(function, merge, MachineForm::Return);
    ret.add_operand(MachineOperand::use(result));

    if (!dgtl::mir::lowering::MachinePhiLowering::run(function)) {
        throw std::runtime_error("critical-edge PHI lowering failed");
    }
    if (function.blocks().size() != 5u ||
        function.instruction(phi_id)->parent_block != INVALID_BLOCK_ID ||
        function.block(left_block)->successors.size() != 2u) {
        throw std::runtime_error("critical-edge PHI lowering left inconsistent block state");
    }
    verify_block_instruction_links(function);
}

} // namespace

void run_test_t15_mir_register_rewrite() {
    test_two_address_reallocation();
    test_instruction_insertion_repairs_stale_owner();
    test_critical_edge_phi_reallocation();
}

int main() {
    try {
        run_test_t15_mir_register_rewrite();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "[T15] FAIL - %s\n", error.what());
        return 1;
    }
    return 0;
}