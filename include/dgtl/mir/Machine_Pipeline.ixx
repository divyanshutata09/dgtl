/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Component:   Unified Machine Backend Compilation Pipeline
 * Standards:   C++26 / Clang 18+ / MSVC 19.40+ / GCC 14+
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */
module;

#include <cstdlib>
#include <algorithm>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <stdexcept>

export module dgtl.mir.pipeline;

import dgtl.ir;
import dgtl.ir_enums;
import dgtl.target;
import dgtl.mir.machine_ir;
import dgtl.mir.legalizer;
import dgtl.mir.instruction_selector;
import dgtl.mir.phi_lowering;
import dgtl.mir.two_address;
import dgtl.mir.regalloc.universal;
import dgtl.mir.target_isel;
import dgtl.mir.module;
import dgtl.mir.inline_spiller;
import dgtl.mir.prolog_epilog_inserter;
import dgtl.mir.frame_index_elimination;

export namespace dgtl::mir::pipeline {

using dgtl::machine::MachineModule;
using dgtl::mir::InlineSpiller;
using dgtl::mir::PrologEpilogInserter;
using dgtl::mir::FrameIndexElimination;

using dgtl::machine::TargetABI;
using dgtl::machine::TargetFrameInfo;
using dgtl::machine::TargetInstructionInfo;
using dgtl::machine::TargetRegisterInfo;

using dgtl::machine::BlockId;
using dgtl::machine::InstrId;
using dgtl::machine::MachineBasicBlock;
using dgtl::machine::MachineInstruction;
using dgtl::machine::MachineOperand;
using dgtl::machine::MachineOperandKind;
using dgtl::machine::MachineSemanticKind;
using dgtl::machine::MachineForm;
using dgtl::machine::MachineFunction;
using dgtl::machine::MachineRegister;
using dgtl::machine::MachineSpiller;
using dgtl::machine::MachineFunctionBuilder;
using dgtl::machine::MachineOpcode;
using dgtl::machine::MachineVerifier;
using dgtl::machine::MachineVerifyError;
using dgtl::machine::RegisterClassKind;
using dgtl::machine::INVALID_BLOCK_ID;
using dgtl::machine::INVALID_INSTR_ID;
using dgtl::machine::GM_NOOP;
using dgtl::machine::GM_COPY;
using dgtl::machine::is_target_opcode;
using dgtl::machine::is_generic_opcode;
using dgtl::mir::isel::InstructionSelectorProgram;
using dgtl::mir::isel::SelectedInstruction;
using dgtl::mir::isel::make_builtin_instruction_selector_program;
using dgtl::mir::lowering::LegalizationResult;
using dgtl::mir::lowering::MachineLegalizer;
using dgtl::mir::lowering::MachinePhiLowering;
using dgtl::mir::lowering::TwoAddressInstructionPass;

class MachinePeepholeOptimizer {
public:
    static bool run(MachineFunction& function) {
        bool modified = false;
        const auto blocks = function.blocks();

        for (std::size_t b_idx = 0; b_idx < blocks.size(); ++b_idx) {
            MachineBasicBlock* bb = function.block(static_cast<BlockId>(b_idx));
            if (!bb || bb->instructions.empty()) continue;

            std::vector<InstrId> to_remove;

            for (std::size_t i = 0; i < bb->instructions.size(); ++i) {
                const InstrId inst_id = bb->instructions[i];
                auto* inst = function.instruction(inst_id);
                if (!inst) continue;

                // Once a concrete target opcode has been assigned, generic
                // peephole rewrites must not mutate the form/opcode pair.
                if (inst->has_target_opcode()) continue;

                // 1. Redundant self-moves: Move %rA, %rA
                if (inst->form == MachineForm::Move && inst->operands.size() >= 2) {
                    const auto& op0 = inst->operands[0];
                    const auto& op1 = inst->operands[1];
                    if (op0.is_register() && op1.is_register() && op0.reg == op1.reg) {
                        inst->form = MachineForm::NoOp;
                        inst->semantic_kind = MachineSemanticKind::Other;
                        inst->opcode = GM_NOOP;
                        inst->operands.clear();
                        to_remove.push_back(inst_id);
                        modified = true;
                        continue;
                    }
                }

                // 2. Identity arithmetic:
                if ((inst->semantic_kind == MachineSemanticKind::IntegerAdd ||
                     inst->semantic_kind == MachineSemanticKind::IntegerSub) &&
                    inst->operands.size() >= 3) {
                    const auto& dest = inst->operands[0];
                    const auto& src = inst->operands[1];
                    const auto& imm_op = inst->operands[2];
                    if (imm_op.is_immediate() && imm_op.immediate == 0) {
                        if (dest.is_register() && src.is_register() && dest.reg == src.reg) {
                            inst->form = MachineForm::NoOp;
                            inst->semantic_kind = MachineSemanticKind::Other;
                            inst->opcode = GM_NOOP;
                            inst->operands.clear();
                            to_remove.push_back(inst_id);
                            modified = true;
                            continue;
                        } else if (dest.is_register() && src.is_register()) {
                            inst->form = MachineForm::Move;
                            inst->semantic_kind = MachineSemanticKind::Move;
                            inst->opcode = GM_COPY;
                            inst->operands.resize(2);
                            modified = true;
                        }
                    }
                }

                if (inst->semantic_kind == MachineSemanticKind::IntegerMul && inst->operands.size() >= 3) {
                    const auto& dest = inst->operands[0];
                    const auto& src = inst->operands[1];
                    const auto& imm_op = inst->operands[2];
                    if (imm_op.is_immediate() && imm_op.immediate == 1) {
                        if (dest.is_register() && src.is_register() && dest.reg == src.reg) {
                            inst->form = MachineForm::NoOp;
                            inst->semantic_kind = MachineSemanticKind::Other;
                            inst->opcode = GM_NOOP;
                            inst->operands.clear();
                            to_remove.push_back(inst_id);
                            modified = true;
                            continue;
                        } else if (dest.is_register() && src.is_register()) {
                            inst->form = MachineForm::Move;
                            inst->semantic_kind = MachineSemanticKind::Move;
                            inst->opcode = GM_COPY;
                            inst->operands.resize(2);
                            modified = true;
                        }
                    }
                }

                // 3. Consecutive reciprocal moves: mov rA, rB; mov rB, rA
                if (i + 1 < bb->instructions.size() && inst->form == MachineForm::Move && inst->operands.size() >= 2) {
                    const InstrId next_id = bb->instructions[i + 1];
                    auto* next_inst = function.instruction(next_id);
                    if (next_inst && next_inst->form == MachineForm::Move && next_inst->operands.size() >= 2) {
                        if (inst->operands[0].is_register() && inst->operands[1].is_register() &&
                            next_inst->operands[0].is_register() && next_inst->operands[1].is_register()) {
                            if (inst->operands[0].reg == next_inst->operands[1].reg &&
                                inst->operands[1].reg == next_inst->operands[0].reg) {
                                next_inst->form = MachineForm::NoOp;
                                next_inst->semantic_kind = MachineSemanticKind::Other;
                                next_inst->opcode = GM_NOOP;
                                next_inst->operands.clear();
                                to_remove.push_back(next_id);
                                modified = true;
                            }
                        }
                    }
                }
            }
            for (const InstrId id : to_remove) {
                function.remove_instruction(id);
            }
        }

        return modified;
    }
};

class BranchFolder {
public:
    static bool run(MachineFunction& function) {
        bool modified = false;
        const auto blocks = function.blocks();
        if (blocks.size() <= 1) return false;

        for (std::size_t b_idx = 0; b_idx < blocks.size(); ++b_idx) {
            const BlockId block_id = static_cast<BlockId>(b_idx);
            MachineBasicBlock* bb = function.block(block_id);
            if (!bb || bb->instructions.empty()) continue;

            const BlockId next_layout_block =
                (b_idx + 1 < blocks.size()) ? static_cast<BlockId>(b_idx + 1) : INVALID_BLOCK_ID;

            std::vector<InstrId> to_remove;
            bool saw_terminator = false;

            for (const InstrId inst_id : bb->instructions) {
                auto* inst = function.instruction(inst_id);
                if (!inst) continue;

                // Branch folding runs again after target lowering. Do not
                // rewrite an instruction whose target opcode is now fixed.
                if (inst->has_target_opcode()) continue;

                if (saw_terminator) {
                    inst->form = MachineForm::NoOp;
                    inst->semantic_kind = MachineSemanticKind::Other;
                    inst->opcode = GM_NOOP;
                    inst->operands.clear();
                    to_remove.push_back(inst_id);
                    modified = true;
                    continue;
                }

                if (inst->form == MachineForm::Goto && !inst->operands.empty()) {
                    const auto& target_op = inst->operands[0];
                    if (target_op.kind == MachineOperandKind::BasicBlock &&
                        target_op.block == next_layout_block) {
                        inst->form = MachineForm::NoOp;
                        inst->semantic_kind = MachineSemanticKind::Other;
                        inst->opcode = GM_NOOP;
                        inst->operands.clear();
                        to_remove.push_back(inst_id);
                        modified = true;
                        continue;
                    }

                    saw_terminator = true;
                } else if (inst->form == MachineForm::Return) {
                    saw_terminator = true;
                }
            }
            for (const InstrId id : to_remove) {
                function.remove_instruction(id);
            }
        }

        return modified;
    }
};

struct MachinePipelineOptions {
    bool run_legalizer = true;
    bool run_phi_lowering = true;
    bool run_two_address = true;
    bool run_peephole = true;
    bool run_regalloc = true;
    bool run_branch_folding = true;
    bool run_frame_lowering = true;
    bool run_abi_lowering = true;
    bool verify_phases = true;
};

struct MachinePipelineResult {
    bool success = false;
    std::string error_message{};
    MachineVerifyError verification_error = MachineVerifyError::None;
    BlockId error_block = INVALID_BLOCK_ID;
    InstrId error_instr = INVALID_INSTR_ID;
};

class MachinePipeline {
public:
    static MachinePipelineResult compile(
        MachineFunction& function,
        const ir::IRStream& stream,
        const core::TargetMachine& target,
        const TargetRegisterInfo& reg_info,
        const TargetInstructionInfo* instr_info,
        const TargetFrameInfo* frame_info,
        const TargetABI* abi,
        const InstructionSelectorProgram& selector = make_builtin_instruction_selector_program(),
        const MachinePipelineOptions& options = {},
        MachineSpiller* spiller = nullptr) {

        MachinePipelineResult result{};

        const auto target_rule_lines = dgtl::mir::rule_lines(target);

        std::vector<std::string_view> generated_rule_lines;
        generated_rule_lines.reserve(target_rule_lines.size());
        generated_rule_lines.insert(
            generated_rule_lines.end(),
            target_rule_lines.begin(),
            target_rule_lines.end());

        const InstructionSelectorProgram generated_selector =
            InstructionSelectorProgram::from_rule_lines(generated_rule_lines);
        const InstructionSelectorProgram& active_selector =
            selector.empty() ? generated_selector : selector;

        // Pass 1: Legalizer
        const ir::IRStream* active_stream = &stream;
        LegalizationResult leg_result;
        if (options.run_legalizer) {
            MachineLegalizer legalizer;
            leg_result = legalizer.legalize(stream, target);
            if (!leg_result.stream.is_null()) {
                active_stream = &leg_result.stream;
            }
        }

        // Pass 2: Instruction Selection (HIR -> MachineFunction)
        MachineFunctionBuilder builder(function);
        BlockId current_block_id = function.create_block(true).id;

        for (std::size_t i = 0; i < active_stream->size(); ++i) {
            const auto& node = active_stream->get_node(i);

            if (node.opcode == ir::OpCode::LABEL) {
                const auto param_results = active_stream->get_result_ids(i);
                const bool is_fn_entry = ((node.flags & 0x0008 /* DGTL_FLAG_IS_VOLATILE */) != 0) || !param_results.empty();

                if (function.block(current_block_id) && 
                    (!function.block(current_block_id)->instructions.empty() || (is_fn_entry && current_block_id != 0))) {
                    const BlockId prev_block_id = current_block_id;
                    current_block_id = function.create_block(is_fn_entry).id;

                    const auto* prev_bb = function.block(prev_block_id);
                    if (prev_bb && !prev_bb->instructions.empty()) {
                        const auto* tail_inst = function.instruction(prev_bb->tail_instr);
                        if (!tail_inst || (tail_inst->form != MachineForm::Return && tail_inst->form != MachineForm::Goto)) {
                            function.connect_blocks(prev_block_id, current_block_id);
                        }
                    }
                } else if (is_fn_entry) {
                    if (auto* curr_bb = function.block(current_block_id)) {
                        curr_bb->entry = true;
                    }
                }

                auto* curr_bb = function.block(current_block_id);
                if (curr_bb && is_fn_entry) {
                    for (const auto slot_id : param_results) {
                        const auto type_code = active_stream->get_result_type_code(slot_id);
                        const bool is_fp = (type_code == ir::TypeCode::F32 || type_code == ir::TypeCode::F64 || type_code == ir::TypeCode::F16 || type_code == ir::TypeCode::F128);
                        const auto reg_class = is_fp ? RegisterClassKind::FloatingPoint : RegisterClassKind::General;
                        MachineRegister param_vreg = builder.value_register(slot_id, reg_class);
                        curr_bb->formal_params.push_back(param_vreg);
                        if (current_block_id == function.entry_block() || current_block_id == 0) {
                            function.add_param(param_vreg);
                        }
                    }
                }

                const std::uint32_t label_id = (node.result_id != dgtl::ir::INVALID_SLOT) ? node.result_id : node.op_a;
                function.map_label_to_block(label_id, current_block_id);
                continue;
            }

            const SelectedInstruction selected =
                active_selector.select_instruction(*active_stream, i, node, target);

            if (selected.form == dgtl::mir::MachineForm::Unhandled) {
                continue;
            }

            (void)active_selector.append_to_machine_function(
                builder, current_block_id, selected, RegisterClassKind::General);
        }

        // Connect CFG edges and resolve IR labels in BasicBlock operands
        for (std::size_t b_idx = 0; b_idx < function.blocks().size(); ++b_idx) {
            MachineBasicBlock* bb = function.block(static_cast<BlockId>(b_idx));
            if (!bb) continue;
            for (const InstrId inst_id : bb->instructions) {
                auto* inst = function.instruction(inst_id);
                if (!inst) continue;
                for (auto& op : inst->operands) {
                    if (op.kind == MachineOperandKind::BasicBlock) {
                        const auto resolved = function.find_block_for_label(op.block);
                        if (resolved.has_value()) {
                            op.block = *resolved;
                        }
                        if (op.block < function.blocks().size()) {
                            function.connect_blocks(bb->id, op.block);
                        }
                    }
                }
            }
        }

        // Pass 3: SSA Deconstruction & Critical-Edge-Aware Phi Lowering
        if (options.run_phi_lowering) {
            if (!MachinePhiLowering::run(function)) {
                result.error_message = "Phi lowering failed during SSA deconstruction";
                return result;
            }
        }

        // Pass 4: Two-Address Normalization
        if (options.run_two_address) {
            bool normalization_succeeded = true;
            TwoAddressInstructionPass::run(function, target, &normalization_succeeded);
            if (!normalization_succeeded) {
                result.error_message = "Two-address normalization failed to insert a required copy";
                return result;
            }
        }

        // Pass 5: Target ABI Lowering (argument passing, calls, returns)
        if (options.run_abi_lowering && abi != nullptr) {
            if (!abi->lower_function(function)) {
                result.error_message = "Target ABI refused function lowering";
                return result;
            }
        }

        // Pass 6: Pre-RA Machine Peephole Optimization
        if (options.run_peephole) {
            MachinePeepholeOptimizer::run(function);
        }

        // Pass 7: Concrete Target Opcode Assignment
        auto ensure_target_opcodes = [&](MachineFunction& fn) -> bool {
            if (instr_info == nullptr) return false;
            for (std::size_t i = 0; i < fn.instructions().size(); ++i) {
                auto* inst = fn.instruction(static_cast<InstrId>(i));
                if (!inst || inst->parent_block == dgtl::machine::INVALID_BLOCK_ID) continue;
                if (!inst->has_target_opcode()) {
                    MachineOpcode target_op = instr_info->opcode_for(inst->form, inst->semantic_kind);
                    if (target_op == dgtl::machine::INVALID_MACHINE_OPCODE) {
                        if (inst->form == MachineForm::Move) target_op = instr_info->opcode_for(MachineForm::Move, MachineSemanticKind::Move);
                        else if (inst->form == MachineForm::StoreStack) target_op = instr_info->opcode_for(MachineForm::StoreStack, MachineSemanticKind::StoreStack);
                        else if (inst->form == MachineForm::LoadStack) target_op = instr_info->opcode_for(MachineForm::LoadStack, MachineSemanticKind::LoadStack);
                        else if (inst->form == MachineForm::Return) target_op = instr_info->opcode_for(MachineForm::Return, MachineSemanticKind::Return);
                    }
                    if (target_op != dgtl::machine::INVALID_MACHINE_OPCODE) {
                        if (!inst->assign_target_opcode_once(target_op)) {
                            return false;
                        }
                    }
                }
            }
            return true;
        };

        if (instr_info != nullptr) {
            if (!ensure_target_opcodes(function)) {
                result.error_message = "MachineFunction failed to assign target opcodes";
                return result;
            }
            if (!function.mark_target_lowered()) {
                result.error_message = "MachineFunction rejected target-lowered phase";
                return result;
            }
        }

        // Pass 8: Register Allocation (with InlineSpiller)
        if (options.run_regalloc) {
            InlineSpiller default_spiller(instr_info);
            MachineSpiller* active_spiller = spiller ? spiller : &default_spiller;

            dgtl::mir::regalloc::UniversalGraphColoringRegAlloc regalloc(
                function, reg_info, instr_info, frame_info, active_spiller);

            if (!regalloc.run()) {
                result.error_message =
                    "Register allocation failed: " +
                    (regalloc.failure_reason().empty()
                        ? std::string("interference graph could not be colored "
                                      "and spill/reload lowering was unavailable or unsuccessful")
                        : regalloc.failure_reason());
                return result;
            }
            if (instr_info != nullptr) {
                ensure_target_opcodes(function);
            }
            function.mark_registers_allocated();
            if (function.has_virtual_registers() ||
                !function.properties().registers_allocated) {
                result.error_message = "Register allocation completed with virtual registers remaining";
                return result;
            }
        }

        // Pass 9: Frame Lowering + Prologue/Epilogue + Frame Index Elimination
        if (options.run_frame_lowering) {
            if (frame_info == nullptr) {
                result.error_message = "Target frame information is required for frame lowering";
                return result;
            }

            auto align_up = [](std::uint64_t value, std::uint64_t alignment) -> std::uint64_t {
                if (alignment <= 1u) return value;
                const std::uint64_t remainder = value % alignment;
                return remainder == 0u ? value : value + (alignment - remainder);
            };

            auto& frame = function.frame_info();
            frame.stack_alignment = std::max<std::uint32_t>(1u, frame_info->stack_alignment());

            std::uint64_t current_offset = 0;
            frame.spill_bytes = 0;
            frame.local_bytes = 0;

            for (auto& slot : frame.slots) {
                const std::uint64_t alignment = std::max<std::uint32_t>(1u, slot.alignment);
                current_offset = align_up(current_offset, alignment);
                slot.offset = static_cast<std::int64_t>(current_offset);
                slot.fixed_offset = true;
                current_offset += slot.size_bytes;

                if (slot.kind == dgtl::machine::StackSlotKind::Spill) {
                    frame.spill_bytes += slot.size_bytes;
                } else {
                    frame.local_bytes += slot.size_bytes;
                }
            }

            frame.total_frame_bytes = static_cast<std::uint32_t>(
                align_up(current_offset, frame.stack_alignment));

            PrologEpilogInserter::run(function, *frame_info);

            const MachineRegister fp = abi ? abi->frame_pointer_register()
                                           : MachineRegister::physical_reg(5u, RegisterClassKind::Address);
            FrameIndexElimination::run(function, *frame_info, fp);

            if (instr_info != nullptr) {
                if (!ensure_target_opcodes(function)) {
                    result.error_message = "MachineFunction failed to assign frame target opcodes";
                    return result;
                }
            }

            if (!function.mark_frame_lowered()) {
                result.error_message = "MachineFunction rejected frame-lowered phase";
                return result;
            }
        }

        // Pass 10: Mark ABI Lowered
        if (options.run_abi_lowering) {
            if (!function.mark_abi_lowered()) {
                result.error_message = "MachineFunction rejected ABI-lowered phase";
                return result;
            }
        }

        // Pass 11: Post-RA Machine Peephole & Branch Folding
        if (options.run_peephole) {
            MachinePeepholeOptimizer::run(function);
        }
        if (options.run_branch_folding) {
            BranchFolder::run(function);
        }

        // Pass 12: Mark Emission Ready & Formal Verification
        if (instr_info != nullptr) {
            ensure_target_opcodes(function);
        }
        if (!function.mark_emission_ready()) {
            result.error_message = "MachineFunction rejected emission-ready phase";
            return result;
        }

        if (options.verify_phases) {
            std::string verify_diag;
            const auto verify_status = MachineVerifier::verify(function, &verify_diag);
            if (verify_status.error != MachineVerifyError::None) {
                result.verification_error = verify_status.error;
                result.error_block = verify_status.block;
                result.error_instr = verify_status.instruction;
                result.error_message = "Machine IR verification failed with error code " +
                    std::to_string(static_cast<uint32_t>(verify_status.error)) +
                    (!verify_diag.empty() ? (" (" + verify_diag + ")") : "");
                return result;
            }
        }

        result.success = true;
        return result;
    }

    static MachinePipelineResult compile(
        MachineModule& module,
        const ir::IRStream& stream,
        const core::TargetMachine& target,
        const TargetRegisterInfo& reg_info,
        const TargetInstructionInfo* instr_info,
        const TargetFrameInfo* frame_info,
        const TargetABI* abi,
        const InstructionSelectorProgram& selector = make_builtin_instruction_selector_program(),
        const MachinePipelineOptions& options = {},
        MachineSpiller* spiller = nullptr) {

        if (module.functions().empty()) {
            std::string_view entry = module.entry_point().empty() ? "main" : module.entry_point();
            module.add_function(entry);
        }

        for (auto& entry : module.functions()) {
            if (!entry.function) continue;
            auto res = compile(*entry.function, stream, target, reg_info, instr_info, frame_info, abi, selector, options, spiller);
            if (!res.success) return res;
        }

        MachinePipelineResult result{};
        result.success = true;
        return result;
    }
};

} // namespace dgtl::mir::pipeline

export namespace dgtl::mir {
    using pipeline::MachinePipeline;
    using pipeline::MachinePipelineOptions;
    using pipeline::MachinePipelineResult;
    using isel::make_builtin_instruction_selector_program;
    using dgtl::machine::MachineModule;
}
