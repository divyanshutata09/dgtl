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

/*
 * DGTL AArch64 target ABI and frame contracts.
 *
 * AAPCS64 register classification is represented here for the common MIR
 * MachineContext. Complex aggregates remain indirect until the frontend emits
 * an explicit ABIValue classification.
 */
module;

#include <algorithm>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>
export module dgtl.isa.arm64.arm64_abi;
import dgtl.mir.machine_ir;
export namespace dgtl::isa::arm64 {

class ARM64FrameInfo final : public dgtl::machine::TargetFrameInfo {
public:
    [[nodiscard]] std::uint32_t stack_alignment() const noexcept override {
        return 16u;
    }

    [[nodiscard]] std::uint32_t
    spill_alignment(dgtl::machine::RegisterClassKind cls) const noexcept override {
        using namespace dgtl::machine;
        switch (cls) {
            case RegisterClassKind::FloatingPoint: return 8u;
            case RegisterClassKind::Vector: return 16u;
            default: return 8u;
        }
    }

    [[nodiscard]] std::uint32_t
    spill_size(dgtl::machine::RegisterClassKind cls) const noexcept override {
        using namespace dgtl::machine;
        switch (cls) {
            case RegisterClassKind::FloatingPoint: return 8u;
            case RegisterClassKind::Vector: return 16u;
            default: return 8u;
        }
    }

    [[nodiscard]] std::uint32_t
    outgoing_argument_alignment() const noexcept override {
        return 8u;
    }
};

class AAPCS64ABI final : public dgtl::machine::TargetABI {
public:
    [[nodiscard]] dgtl::machine::ABICallPlan classify_call(
        std::span<const dgtl::machine::ABIValue> arguments,
        std::optional<dgtl::machine::ABIValue> return_value) const override {

        using namespace dgtl::machine;

        ABICallPlan plan{};
        plan.stack_alignment = 16u;

        std::uint32_t next_gpr = 0;
        std::uint32_t next_fp = 0;
        std::uint32_t stack_offset = 0;

        for (const ABIValue& value : arguments) {
            const bool fp =
                value.kind == ABIValueKind::FloatingPoint ||
                value.kind == ABIValueKind::Vector ||
                value.preferred_class == RegisterClassKind::FloatingPoint ||
                value.preferred_class == RegisterClassKind::Vector;

            const std::uint32_t size =
                std::max<std::uint32_t>(value.size_bytes, 1u);
            const std::uint32_t aligned =
                (size + 7u) & ~7u;

            ABIArgLocation location{};
            location.size_bytes = size;

            if (fp && next_fp < 8u) {
                location.kind = ABIArgLocationKind::Register;
                location.reg = {
                    32u + next_fp,
                    RegisterKind::Physical,
                    value.preferred_class == RegisterClassKind::Vector
                        ? RegisterClassKind::Vector
                        : RegisterClassKind::FloatingPoint
                };
                ++next_fp;
            } else if (!fp && size <= 8u && next_gpr < 8u) {
                location.kind = ABIArgLocationKind::Register;
                location.reg = {
                    next_gpr,
                    RegisterKind::Physical,
                    value.kind == ABIValueKind::Pointer
                        ? RegisterClassKind::Address
                        : RegisterClassKind::General
                };
                ++next_gpr;
            } else {
                location.kind = ABIArgLocationKind::Stack;
                location.stack_offset = static_cast<std::int32_t>(stack_offset);
                stack_offset += aligned;
            }

            plan.arguments.push_back(location);
        }

        plan.stack_bytes =
            (stack_offset + 15u) & ~15u;

        if (return_value.has_value()) {
            const ABIValue& value = *return_value;
            const bool fp =
                value.kind == ABIValueKind::FloatingPoint ||
                value.kind == ABIValueKind::Vector ||
                value.preferred_class == RegisterClassKind::FloatingPoint ||
                value.preferred_class == RegisterClassKind::Vector;

            if (fp) {
                plan.return_value = {
                    ABIArgLocationKind::Register,
                    {
                        32u,
                        RegisterKind::Physical,
                        value.preferred_class == RegisterClassKind::Vector
                            ? RegisterClassKind::Vector
                            : RegisterClassKind::FloatingPoint
                    },
                    0,
                    value.size_bytes
                };
            } else {
                plan.return_value = {
                    ABIArgLocationKind::Register,
                    {
                        0u,
                        RegisterKind::Physical,
                        value.kind == ABIValueKind::Pointer
                            ? RegisterClassKind::Address
                            : RegisterClassKind::General
                    },
                    0,
                    value.size_bytes
                };
            }
        }

        return plan;
    }

    [[nodiscard]] bool is_caller_saved(
        dgtl::machine::MachineRegister reg) const noexcept override {

        using namespace dgtl::machine;

        if (!reg.is_physical()) return false;

        if (reg.reg_class == RegisterClassKind::General ||
            reg.reg_class == RegisterClassKind::Integer ||
            reg.reg_class == RegisterClassKind::Address) {
            return reg.id <= 18u || reg.id == 30u;
        }

        if (reg.reg_class == RegisterClassKind::FloatingPoint ||
            reg.reg_class == RegisterClassKind::Vector) {
            return (reg.id >= 32u && reg.id <= 39u) ||
                   (reg.id >= 48u && reg.id <= 63u);
        }

        return false;
    }

    [[nodiscard]] bool is_callee_saved(
        dgtl::machine::MachineRegister reg) const noexcept override {

        using namespace dgtl::machine;

        if (!reg.is_physical()) return false;

        if (reg.reg_class == RegisterClassKind::General ||
            reg.reg_class == RegisterClassKind::Integer ||
            reg.reg_class == RegisterClassKind::Address) {
            return reg.id >= 19u && reg.id <= 28u;
        }

        if (reg.reg_class == RegisterClassKind::FloatingPoint ||
            reg.reg_class == RegisterClassKind::Vector) {
            return reg.id >= 40u && reg.id <= 47u;
        }

        return false;
    }

    [[nodiscard]] dgtl::machine::MachineRegister stack_pointer_register() const noexcept override {
        return {
            31u,
            dgtl::machine::RegisterKind::Physical,
            dgtl::machine::RegisterClassKind::Special
        };
    }

    [[nodiscard]] dgtl::machine::MachineRegister frame_pointer_register() const noexcept override {
        return {
            29u,
            dgtl::machine::RegisterKind::Physical,
            dgtl::machine::RegisterClassKind::Address
        };
    }

    [[nodiscard]] static constexpr std::int32_t get_aapcs64_caller_stack_arg_offset(std::uint32_t arg_index) noexcept {
        constexpr std::uint32_t SLOT_SIZE = 8u;
        return static_cast<std::int32_t>((arg_index - 8u) * SLOT_SIZE);
    }

    [[nodiscard]] static constexpr std::int32_t get_aapcs64_callee_stack_arg_offset(std::uint32_t arg_index) noexcept {
        constexpr std::uint32_t FRAME_HEADER = 16u; // saved FP (X29) + saved LR (X30)
        constexpr std::uint32_t SLOT_SIZE = 8u;
        return static_cast<std::int32_t>(FRAME_HEADER + (arg_index - 8u) * SLOT_SIZE);
    }

    bool lower_formal_arguments_for_block(
        dgtl::machine::MachineFunction& function,
        dgtl::machine::BlockId block_id,
        std::span<const dgtl::machine::MachineRegister> param_vregs) const {
        using namespace dgtl::machine;
        if (param_vregs.empty() || function.blocks().empty()) return true;

        MachineBasicBlock* entry = function.block(block_id);
        if (!entry) return true;

        const InstrId cursor = entry->instructions.empty() ? INVALID_INSTR_ID : entry->instructions.front();

        std::size_t gpr_idx = 0;
        std::size_t fp_idx = 0;

        for (std::size_t i = 0; i < param_vregs.size(); ++i) {
            MachineRegister param_vreg = param_vregs[i];
            const bool is_fp = (param_vreg.reg_class == RegisterClassKind::FloatingPoint ||
                                param_vreg.reg_class == RegisterClassKind::Vector);

            if (is_fp && fp_idx < 8u) {
                const std::uint32_t reg_id = 32u + static_cast<std::uint32_t>(fp_idx++);
                MachineRegister phys = MachineRegister::physical_reg(reg_id, param_vreg.reg_class);

                MachineInstruction& copy_inst = function.create_instruction(GM_COPY);
                copy_inst.form = MachineForm::Move;
                copy_inst.semantic_kind = MachineSemanticKind::Move;
                copy_inst.add_operand(MachineOperand::def(param_vreg));
                copy_inst.add_operand(MachineOperand::use(phys));
                copy_inst.parent_block = block_id;
                if (!function.insert_instruction_before(block_id, cursor, copy_inst.id)) {
                    return false;
                }
            } else if (!is_fp && gpr_idx < 8u) {
                const std::uint32_t reg_id = static_cast<std::uint32_t>(gpr_idx++);
                MachineRegister phys = MachineRegister::physical_reg(reg_id, param_vreg.reg_class);

                MachineInstruction& copy_inst = function.create_instruction(GM_COPY);
                copy_inst.form = MachineForm::Move;
                copy_inst.semantic_kind = MachineSemanticKind::Move;
                copy_inst.add_operand(MachineOperand::def(param_vreg));
                copy_inst.add_operand(MachineOperand::use(phys));
                copy_inst.parent_block = block_id;
                if (!function.insert_instruction_before(block_id, cursor, copy_inst.id)) {
                    return false;
                }
            } else {
                // Stack parameter (i >= 8)
                const std::uint32_t stack_idx = static_cast<std::uint32_t>(is_fp ? (fp_idx++) : (gpr_idx++));
                const std::int64_t disp = get_aapcs64_callee_stack_arg_offset(stack_idx);

                MachineInstruction& load_inst = function.create_instruction(GM_LOAD);
                load_inst.form = MachineForm::LoadStack;
                load_inst.semantic_kind = MachineSemanticKind::LoadStack;
                MachineMemoryOperand mem{};
                mem.base = MachineRegister{29u, RegisterKind::Physical, RegisterClassKind::General}; // FP (X29)
                mem.displacement = disp;
                mem.size_bytes = 8;
                mem.load = true;
                load_inst.add_operand(MachineOperand::def(param_vreg));
                load_inst.add_operand(MachineOperand::memory_ref(mem));
                load_inst.parent_block = block_id;
                if (!function.insert_instruction_before(block_id, cursor, load_inst.id)) {
                    return false;
                }
            }
        }
        return true;
    }

    bool lower_formal_arguments(
        dgtl::machine::MachineFunction& function,
        std::span<const dgtl::machine::MachineRegister> param_vregs) const override {
        using namespace dgtl::machine;
        BlockId entry_id = function.entry_block();
        if (entry_id == INVALID_BLOCK_ID) entry_id = 0;
        return lower_formal_arguments_for_block(function, entry_id, param_vregs);
    }

    bool lower_call(
        dgtl::machine::MachineFunction& function,
        dgtl::machine::BlockId block,
        dgtl::machine::InstrId call_instr) const override {
        using namespace dgtl::machine;
        MachineInstruction* inst = function.instruction(call_instr);
        if (!inst) return false;

        std::vector<MachineOperand> arg_operands;
        for (const auto& op : inst->operands) {
            if (op.is_register() && op.is_use()) {
                arg_operands.push_back(op);
            }
        }

        std::size_t gpr_idx = 0;
        std::size_t fp_idx = 0;
        MachineRegister sp{31u, RegisterKind::Physical, RegisterClassKind::Special};

        for (std::size_t i = 0; i < arg_operands.size(); ++i) {
            const bool is_fp = (arg_operands[i].reg.reg_class == RegisterClassKind::FloatingPoint ||
                                arg_operands[i].reg.reg_class == RegisterClassKind::Vector);

            if (is_fp && fp_idx < 8u) {
                const std::uint32_t reg_id = 32u + static_cast<std::uint32_t>(fp_idx++);
                MachineRegister phys = MachineRegister::physical_reg(reg_id, arg_operands[i].reg.reg_class);
                MachineInstruction& copy_inst = function.create_instruction(GM_COPY);
                copy_inst.form = MachineForm::Move;
                copy_inst.semantic_kind = MachineSemanticKind::Move;
                copy_inst.add_operand(MachineOperand::def(phys));
                copy_inst.add_operand(MachineOperand::use(arg_operands[i].reg));
                copy_inst.parent_block = block;
                if (!function.insert_instruction_before(block, call_instr, copy_inst.id)) {
                    return false;
                }
            } else if (!is_fp && gpr_idx < 8u) {
                const std::uint32_t reg_id = static_cast<std::uint32_t>(gpr_idx++);
                MachineRegister phys = MachineRegister::physical_reg(reg_id, arg_operands[i].reg.reg_class);
                MachineInstruction& copy_inst = function.create_instruction(GM_COPY);
                copy_inst.form = MachineForm::Move;
                copy_inst.semantic_kind = MachineSemanticKind::Move;
                copy_inst.add_operand(MachineOperand::def(phys));
                copy_inst.add_operand(MachineOperand::use(arg_operands[i].reg));
                copy_inst.parent_block = block;
                if (!function.insert_instruction_before(block, call_instr, copy_inst.id)) {
                    return false;
                }
            } else {
                const std::uint32_t stack_idx = static_cast<std::uint32_t>(is_fp ? (fp_idx++) : (gpr_idx++));
                const std::int64_t disp = get_aapcs64_caller_stack_arg_offset(stack_idx);
                MachineInstruction& store_inst = function.create_instruction(GM_STORE);
                store_inst.form = MachineForm::StoreStack;
                store_inst.semantic_kind = MachineSemanticKind::StoreStack;
                MachineMemoryOperand mem{};
                mem.base = sp;
                mem.displacement = disp;
                mem.size_bytes = 8;
                mem.store = true;
                store_inst.add_operand(MachineOperand::memory_ref(mem));
                store_inst.add_operand(MachineOperand::use(arg_operands[i].reg));
                store_inst.parent_block = block;
                if (!function.insert_instruction_before(block, call_instr, store_inst.id)) {
                    return false;
                }
            }
        }

        const std::uint32_t stack_args = static_cast<std::uint32_t>(
            (gpr_idx > 8u ? (gpr_idx - 8u) : 0u) + (fp_idx > 8u ? (fp_idx - 8u) : 0u));
        const std::uint32_t call_outgoing_bytes = (stack_args * 8u + 15u) & ~15u;
        function.frame_info().has_calls = true;
        if (call_outgoing_bytes > function.frame_info().outgoing_argument_bytes) {
            function.frame_info().outgoing_argument_bytes = call_outgoing_bytes;
        }

        inst = function.instruction(call_instr);
        if (!inst) return false;

        // Caller-saved registers in AAPCS64: X0-X18, X30 (LR)
        for (std::uint32_t reg_id = 0u; reg_id <= 18u; ++reg_id) {
            MachineOperand clobber;
            clobber.kind = MachineOperandKind::Register;
            clobber.role = OperandRole::ImplicitDef;
            clobber.reg = MachineRegister::physical_reg(reg_id, RegisterClassKind::General);
            inst->add_operand(clobber);
        }
        {
            MachineOperand lr_clobber;
            lr_clobber.kind = MachineOperandKind::Register;
            lr_clobber.role = OperandRole::ImplicitDef;
            lr_clobber.reg = MachineRegister::physical_reg(30u, RegisterClassKind::General); // LR
            inst->add_operand(lr_clobber);
        }
        for (std::uint32_t v_id = 0u; v_id < 8u; ++v_id) {
            MachineOperand clobber;
            clobber.kind = MachineOperandKind::Register;
            clobber.role = OperandRole::ImplicitDef;
            clobber.reg = MachineRegister::physical_reg(32u + v_id, RegisterClassKind::FloatingPoint);
            inst->add_operand(clobber);
        }

        const MachineOperand* def_op = inst->get_def();
        if (def_op && def_op->reg.is_virtual()) {
            MachineRegister result_vreg = def_op->reg;
            const bool is_fp = (result_vreg.reg_class == RegisterClassKind::FloatingPoint ||
                                result_vreg.reg_class == RegisterClassKind::Vector);
            const std::uint32_t ret_id = is_fp ? 32u : 0u; // V0 or X0
            MachineRegister phys_ret = MachineRegister::physical_reg(ret_id, result_vreg.reg_class);

            const InstrId next_cursor = inst->next_instr;
            MachineInstruction& copy_res = function.create_instruction(GM_COPY);
            copy_res.form = MachineForm::Move;
            copy_res.semantic_kind = MachineSemanticKind::Move;
            copy_res.add_operand(MachineOperand::def(result_vreg));
            copy_res.add_operand(MachineOperand::use(phys_ret));
            copy_res.parent_block = block;
            if (!function.insert_instruction_before(block, next_cursor, copy_res.id)) {
                return false;
            }
        }

        return true;
    }

    bool lower_return(
        dgtl::machine::MachineFunction& function,
        dgtl::machine::BlockId block,
        dgtl::machine::InstrId ret_instr) const override {
        using namespace dgtl::machine;
        MachineInstruction* inst = function.instruction(ret_instr);
        if (!inst) return false;

        for (const auto& op : inst->operands) {
            if (op.is_register() && op.is_use()) {
                const bool is_fp = (op.reg.reg_class == RegisterClassKind::FloatingPoint ||
                                    op.reg.reg_class == RegisterClassKind::Vector);
                const std::uint32_t ret_id = is_fp ? 32u : 0u;
                MachineRegister phys_ret = MachineRegister::physical_reg(ret_id, op.reg.reg_class);
                MachineInstruction& copy_inst = function.create_instruction(GM_COPY);
                copy_inst.form = MachineForm::Move;
                copy_inst.semantic_kind = MachineSemanticKind::Move;
                copy_inst.add_operand(MachineOperand::def(phys_ret));
                copy_inst.add_operand(MachineOperand::use(op.reg));
                copy_inst.parent_block = block;
                if (!function.insert_instruction_before(block, ret_instr, copy_inst.id)) {
                    return false;
                }
                break;
            }
        }
        return true;
    }

    [[nodiscard]] bool lower_function(
        dgtl::machine::MachineFunction& function) const override {
        using namespace dgtl::machine;

        BlockId entry_id = function.entry_block();
        if (entry_id == INVALID_BLOCK_ID && !function.blocks().empty()) entry_id = 0;
        if (entry_id != INVALID_BLOCK_ID) {
            MachineBasicBlock* entry = function.block(entry_id);
            if (entry && !entry->live_ins.empty()) {
                std::vector<MachineRegister> params = entry->live_ins;
                if (!lower_formal_arguments_for_block(function, entry_id, params)) {
                    return false;
                }
            }
        }

        for (std::size_t b_idx = 0; b_idx < function.blocks().size(); ++b_idx) {
            MachineBasicBlock* bb = function.block(static_cast<BlockId>(b_idx));
            if (!bb) continue;

            const std::vector<InstrId> instructions = bb->instructions;
            for (const InstrId id : instructions) {
                MachineInstruction* inst = function.instruction(id);
                if (!inst) continue;
                if (inst->is_call()) {
                    if (!lower_call(function, bb->id, id)) return false;
                } else if (inst->is_return()) {
                    if (!lower_return(function, bb->id, id)) return false;
                }
            }
        }
        return true;
    }
};

} // namespace dgtl::isa::arm64
