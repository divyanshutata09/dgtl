/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Component:   X64 Target ABI and Frame Lowering Contracts
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
#include <vector>
#include <stdexcept>
export module dgtl.isa.x64.x64_abi;
import dgtl.mir.machine_ir;
export namespace dgtl::isa::x64 {

class X64FrameInfo final : public dgtl::machine::TargetFrameInfo {
public:
    [[nodiscard]] std::uint32_t stack_alignment() const noexcept override {
        return 16u;
    }

    [[nodiscard]] std::uint32_t
    spill_alignment(dgtl::machine::RegisterClassKind cls) const noexcept override {
        using namespace dgtl::machine;
        switch (cls) {
            case RegisterClassKind::Vector: return 64u; // AVX-512 alignment
            case RegisterClassKind::FloatingPoint: return 8u;
            default: return 8u;
        }
    }

    [[nodiscard]] std::uint32_t
    spill_size(dgtl::machine::RegisterClassKind cls) const noexcept override {
        using namespace dgtl::machine;
        switch (cls) {
            case RegisterClassKind::Vector: return 64u; // AVX-512 ZMM slot
            case RegisterClassKind::FloatingPoint: return 8u;
            default: return 8u;
        }
    }

    [[nodiscard]] std::uint32_t
    outgoing_argument_alignment() const noexcept override {
        return 8u;
    }

    void emit_prologue(dgtl::machine::MachineFunction& function) const override {
        using namespace dgtl::machine;
        BlockId entry_id = function.entry_block();
        if (entry_id == INVALID_BLOCK_ID) entry_id = 0;
        emit_prologue(function, entry_id);
    }

    void emit_prologue(dgtl::machine::MachineFunction& function, dgtl::machine::BlockId entry_id) const override {
        using namespace dgtl::machine;
        if (function.blocks().empty()) return;
        MachineBasicBlock* entry = function.block(entry_id);
        if (!entry) return;

        const InstrId cursor = entry->instructions.empty() ? INVALID_INSTR_ID : entry->instructions.front();
        auto& frame = function.frame_info();

        // Discover Win64 non-volatile registers clobbered by the function if not already populated
        if (frame.callee_saved_registers.empty()) {
            for (const auto& instruction : function.instructions()) {
                for (const auto& operand : instruction.operands) {
                    if (operand.kind != MachineOperandKind::Register || !operand.reg.is_physical())
                        continue;
                    if (operand.reg.reg_class != RegisterClassKind::General &&
                        operand.reg.reg_class != RegisterClassKind::Integer &&
                        operand.reg.reg_class != RegisterClassKind::Address)
                        continue;
                    const auto id = operand.reg.id;
                    // Win64 non-volatile GPRs: RBX=3, RSI=6, RDI=7, R12=12, R13=13, R14=14, R15=15
                    if (id == 3u || id == 6u || id == 7u || id == 12u || id == 13u || id == 14u || id == 15u) {
                        if (std::none_of(frame.callee_saved_registers.begin(), frame.callee_saved_registers.end(),
                                         [&](const auto& r) { return r.id == id; })) {
                            frame.callee_saved_registers.push_back(operand.reg);
                        }
                    }
                }
            }
            std::sort(frame.callee_saved_registers.begin(), frame.callee_saved_registers.end(),
                      [](const auto& a, const auto& b) { return a.id < b.id; });
            frame.callee_saved_bytes = static_cast<std::uint32_t>(frame.callee_saved_registers.size() * 8u);
        }

        const std::uint32_t saved_bytes = static_cast<std::uint32_t>(frame.callee_saved_registers.size() * 8u);
        std::uint32_t needed_bytes = frame.total_frame_bytes;
        const std::uint32_t outgoing_bytes = frame.has_calls ? std::max<std::uint32_t>(32u, frame.outgoing_argument_bytes) : 0u;
        if (needed_bytes < outgoing_bytes) {
            needed_bytes = outgoing_bytes;
        }
        const std::uint32_t total_needed = saved_bytes + needed_bytes;
        const std::uint32_t frame_bytes = (total_needed + 15u) & ~15u;

        MachineRegister rsp{4u, RegisterKind::Physical, RegisterClassKind::Address};
        MachineRegister rbp{5u, RegisterKind::Physical, RegisterClassKind::Address};

        // push rbp -> mov [rsp - 8], rbp; sub rsp, 8
        MachineInstruction& push_rbp = function.create_instruction(GM_STORE);
        push_rbp.form = MachineForm::StoreStack;
        push_rbp.semantic_kind = MachineSemanticKind::StoreStack;
        push_rbp.add_flag(MachineInstrFlag::FrameSetup);
        MachineMemoryOperand mem_push{};
        mem_push.base = rsp;
        mem_push.displacement = -8;
        mem_push.size_bytes = 8;
        mem_push.store = true;
        push_rbp.add_operand(MachineOperand::memory_ref(mem_push));
        push_rbp.add_operand(MachineOperand::use(rbp));
        push_rbp.parent_block = entry_id;
        if (!function.insert_instruction_before(entry_id, cursor, push_rbp.id)) {
            throw std::logic_error("DGTL x64 ABI invariant violation: failed to insert push rbp in prologue");
        }

        MachineInstruction& sub_rsp8 = function.create_instruction(GM_ISUB64);
        sub_rsp8.form = MachineForm::IntegerBinary64;
        sub_rsp8.semantic_kind = MachineSemanticKind::IntegerSub;
        sub_rsp8.add_flag(MachineInstrFlag::FrameSetup);
        sub_rsp8.add_operand(MachineOperand::def(rsp));
        sub_rsp8.add_operand(MachineOperand::use(rsp));
        sub_rsp8.add_operand(MachineOperand::imm(8));
        sub_rsp8.parent_block = entry_id;
        if (!function.insert_instruction_before(entry_id, cursor, sub_rsp8.id)) {
            throw std::logic_error("DGTL x64 ABI invariant violation: failed to insert sub rsp, 8 in prologue");
        }

        // mov rbp, rsp
        MachineInstruction& mov_rbp = function.create_instruction(GM_COPY);
        mov_rbp.form = MachineForm::Move;
        mov_rbp.semantic_kind = MachineSemanticKind::Move;
        mov_rbp.add_flag(MachineInstrFlag::FrameSetup);
        mov_rbp.add_operand(MachineOperand::def(rbp));
        mov_rbp.add_operand(MachineOperand::use(rsp));
        mov_rbp.parent_block = entry_id;
        if (!function.insert_instruction_before(entry_id, cursor, mov_rbp.id)) {
            throw std::logic_error("DGTL x64 ABI invariant violation: failed to insert mov rbp, rsp in prologue");
        }

        // Save non-volatile registers to [rbp - 8*(i+1)]
        for (std::size_t i = 0; i < frame.callee_saved_registers.size(); ++i) {
            const auto& reg = frame.callee_saved_registers[i];
            const std::int64_t disp = -static_cast<std::int64_t>((i + 1) * 8u);
            MachineInstruction& save_reg = function.create_instruction(GM_STORE);
            save_reg.form = MachineForm::StoreStack;
            save_reg.semantic_kind = MachineSemanticKind::StoreStack;
            save_reg.add_flag(MachineInstrFlag::FrameSetup);
            MachineMemoryOperand mem_save{};
            mem_save.base = rbp;
            mem_save.displacement = disp;
            mem_save.size_bytes = 8;
            mem_save.store = true;
            save_reg.add_operand(MachineOperand::memory_ref(mem_save));
            save_reg.add_operand(MachineOperand::use(reg));
            save_reg.parent_block = entry_id;
            if (!function.insert_instruction_before(entry_id, cursor, save_reg.id)) {
                throw std::logic_error("DGTL x64 ABI invariant violation: failed to insert callee-saved register save in prologue");
            }
        }

        if (frame_bytes > 0) {
            // sub rsp, frame_bytes
            MachineInstruction& sub_frame = function.create_instruction(GM_ISUB64);
            sub_frame.form = MachineForm::IntegerBinary64;
            sub_frame.semantic_kind = MachineSemanticKind::IntegerSub;
            sub_frame.add_flag(MachineInstrFlag::FrameSetup);
            sub_frame.add_operand(MachineOperand::def(rsp));
            sub_frame.add_operand(MachineOperand::use(rsp));
            sub_frame.add_operand(MachineOperand::imm(frame_bytes));
            sub_frame.parent_block = entry_id;
            if (!function.insert_instruction_before(entry_id, cursor, sub_frame.id)) {
                throw std::logic_error("DGTL x64 ABI invariant violation: failed to insert frame allocation in prologue");
            }
        }
    }

    void emit_epilogue(dgtl::machine::MachineFunction& function, dgtl::machine::BlockId return_block) const override {
        using namespace dgtl::machine;
        MachineBasicBlock* bb = function.block(return_block);
        if (!bb || bb->instructions.empty()) return;

        const InstrId ret_id = bb->tail_instr;
        auto& frame = function.frame_info();

        MachineRegister rsp{4u, RegisterKind::Physical, RegisterClassKind::Address};
        MachineRegister rbp{5u, RegisterKind::Physical, RegisterClassKind::Address};

        // Restore non-volatile registers from [rbp - 8*(i+1)]
        for (std::size_t i = 0; i < frame.callee_saved_registers.size(); ++i) {
            const auto& reg = frame.callee_saved_registers[i];
            const std::int64_t disp = -static_cast<std::int64_t>((i + 1) * 8u);
            MachineInstruction& restore_reg = function.create_instruction(GM_LOAD);
            restore_reg.form = MachineForm::LoadStack;
            restore_reg.semantic_kind = MachineSemanticKind::LoadStack;
            restore_reg.add_flag(MachineInstrFlag::FrameDestroy);
            MachineMemoryOperand mem_restore{};
            mem_restore.base = rbp;
            mem_restore.displacement = disp;
            mem_restore.size_bytes = 8;
            mem_restore.load = true;
            restore_reg.add_operand(MachineOperand::def(reg));
            restore_reg.add_operand(MachineOperand::memory_ref(mem_restore));
            restore_reg.parent_block = return_block;
            if (!function.insert_instruction_before(return_block, ret_id, restore_reg.id)) {
                throw std::logic_error("DGTL x64 ABI invariant violation: failed to insert callee-saved register restore in epilogue");
            }
        }

        // mov rsp, rbp
        MachineInstruction& mov_rsp = function.create_instruction(GM_COPY);
        mov_rsp.form = MachineForm::Move;
        mov_rsp.semantic_kind = MachineSemanticKind::Move;
        mov_rsp.add_flag(MachineInstrFlag::FrameDestroy);
        mov_rsp.add_operand(MachineOperand::def(rsp));
        mov_rsp.add_operand(MachineOperand::use(rbp));
        mov_rsp.parent_block = return_block;
        if (!function.insert_instruction_before(return_block, ret_id, mov_rsp.id)) {
            throw std::logic_error("DGTL x64 ABI invariant violation: failed to insert mov rsp, rbp in epilogue");
        }

        // pop rbp -> mov rbp, [rsp]; add rsp, 8
        MachineInstruction& pop_rbp = function.create_instruction(GM_LOAD);
        pop_rbp.form = MachineForm::LoadStack;
        pop_rbp.semantic_kind = MachineSemanticKind::LoadStack;
        pop_rbp.add_flag(MachineInstrFlag::FrameDestroy);
        MachineMemoryOperand mem_pop{};
        mem_pop.base = rsp;
        mem_pop.displacement = 0;
        mem_pop.size_bytes = 8;
        mem_pop.load = true;
        pop_rbp.add_operand(MachineOperand::def(rbp));
        pop_rbp.add_operand(MachineOperand::memory_ref(mem_pop));
        pop_rbp.parent_block = return_block;
        if (!function.insert_instruction_before(return_block, ret_id, pop_rbp.id)) {
            throw std::logic_error("DGTL x64 ABI invariant violation: failed to insert pop rbp in epilogue");
        }

        MachineInstruction& add_rsp8 = function.create_instruction(GM_IADD64);
        add_rsp8.form = MachineForm::IntegerBinary64;
        add_rsp8.semantic_kind = MachineSemanticKind::IntegerAdd;
        add_rsp8.add_flag(MachineInstrFlag::FrameDestroy);
        add_rsp8.add_operand(MachineOperand::def(rsp));
        add_rsp8.add_operand(MachineOperand::use(rsp));
        add_rsp8.add_operand(MachineOperand::imm(8));
        add_rsp8.parent_block = return_block;
        if (!function.insert_instruction_before(return_block, ret_id, add_rsp8.id)) {
            throw std::logic_error("DGTL x64 ABI invariant violation: failed to insert add rsp, 8 in epilogue");
        }
    }
};

/**
 * @brief Microsoft x64 (Win64) Calling Convention ABI.
 * Mandatory 32-byte shadow store, RCX/RDX/R8/R9 integer and XMM0-XMM3 FP passing.
 */
class Win64ABI final : public dgtl::machine::TargetABI {
public:
    [[nodiscard]] dgtl::machine::ABICallPlan classify_call(
        std::span<const dgtl::machine::ABIValue> arguments,
        std::optional<dgtl::machine::ABIValue> return_value) const override {

        using namespace dgtl::machine;

        ABICallPlan plan{};
        plan.stack_alignment = 16u;
        plan.shadow_space_bytes = 32u; // 32-byte shadow space mandatory for Win64

        // Win64 parameter registers: RCX (1), RDX (2), R8 (8), R9 (9)
        static constexpr std::uint32_t kWin64GprArgs[4] = {1u, 2u, 8u, 9u};

        std::uint32_t arg_index = 0;
        std::uint32_t stack_offset = 32u; // starts after the 32-byte shadow space

        for (const ABIValue& value : arguments) {
            const bool fp =
                value.kind == ABIValueKind::FloatingPoint ||
                value.kind == ABIValueKind::Vector ||
                value.preferred_class == RegisterClassKind::FloatingPoint ||
                value.preferred_class == RegisterClassKind::Vector;

            const std::uint32_t size = std::max<std::uint32_t>(value.size_bytes, 1u);
            const std::uint32_t aligned = (size + 7u) & ~7u;

            ABIArgLocation location{};
            location.size_bytes = size;

            if (arg_index < 4u) {
                location.kind = ABIArgLocationKind::Register;
                if (fp) {
                    location.reg = {
                        value.preferred_class == RegisterClassKind::Vector
                            ? (16u + arg_index) // ZMM0-ZMM3
                            : arg_index,        // XMM0-XMM3
                        RegisterKind::Physical,
                        value.preferred_class == RegisterClassKind::Vector
                            ? RegisterClassKind::Vector
                            : RegisterClassKind::FloatingPoint
                    };
                } else {
                    location.reg = {
                        kWin64GprArgs[arg_index],
                        RegisterKind::Physical,
                        value.kind == ABIValueKind::Pointer
                            ? RegisterClassKind::Address
                            : RegisterClassKind::General
                    };
                }
            } else {
                location.kind = ABIArgLocationKind::Stack;
                location.stack_offset = static_cast<std::int32_t>(stack_offset);
                stack_offset += aligned;
            }

            plan.arguments.push_back(location);
            ++arg_index;
        }

        plan.stack_bytes = (stack_offset + 15u) & ~15u;

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
                        value.preferred_class == RegisterClassKind::Vector ? 16u : 0u,
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
                        0u, // RAX
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
        if (reg.reg_class == RegisterClassKind::FloatingPoint) {
            return reg.id < 6u; // XMM0-XMM5 volatile
        }
        if (reg.reg_class == RegisterClassKind::Vector) {
            return (reg.id >= 16u && reg.id < 22u) || (reg.id >= 32u && reg.id < 48u);
        }
        // RAX=0, RCX=1, RDX=2, R8=8, R9=9, R10=10, R11=11
        return reg.id == 0u || reg.id == 1u || reg.id == 2u ||
               reg.id == 8u || reg.id == 9u || reg.id == 10u || reg.id == 11u;
    }

    [[nodiscard]] bool is_callee_saved(
        dgtl::machine::MachineRegister reg) const noexcept override {
        using namespace dgtl::machine;
        if (!reg.is_physical()) return false;
        if (reg.reg_class == RegisterClassKind::FloatingPoint) {
            return reg.id >= 6u && reg.id < 16u; // XMM6-XMM15 nonvolatile
        }
        if (reg.reg_class == RegisterClassKind::Vector) {
            return reg.id >= 22u && reg.id < 32u; // ZMM6-ZMM15 nonvolatile
        }
        // RBX=3, RBP=5, RSI=6, RDI=7, R12=12, R13=13, R14=14, R15=15
        return reg.id == 3u || reg.id == 5u || reg.id == 6u || reg.id == 7u ||
               reg.id == 12u || reg.id == 13u || reg.id == 14u || reg.id == 15u;
    }

    [[nodiscard]] dgtl::machine::MachineRegister
    stack_pointer_register() const noexcept override {
        return {4u, dgtl::machine::RegisterKind::Physical, dgtl::machine::RegisterClassKind::Address}; // RSP
    }

    [[nodiscard]] dgtl::machine::MachineRegister
    frame_pointer_register() const noexcept override {
        return {5u, dgtl::machine::RegisterKind::Physical, dgtl::machine::RegisterClassKind::Address}; // RBP
    }

    // Windows x64 ABI stack argument helpers
    static constexpr std::uint32_t SHADOW_SPACE_BYTES = 32u;
    static constexpr std::uint32_t SLOT_SIZE = 8u;

    // Caller side: writing the 5th argument onward before a CALL instruction
    // arg_index is 0-based across all arguments (i.e. arg_index >= 4)
    [[nodiscard]] static constexpr std::int32_t get_caller_stack_arg_offset(std::uint32_t arg_index) noexcept {
        return static_cast<std::int32_t>(SHADOW_SPACE_BYTES + (arg_index - 4u) * SLOT_SIZE);
        // Stores to: [RSP + offset]
    }

    // Callee side: loading the 5th argument onward inside the function
    // With standard RBP frame (push rbp [8] + ret_addr [8] + shadow_space [32] = 48)
    [[nodiscard]] static constexpr std::int32_t get_callee_stack_arg_offset(std::uint32_t arg_index) noexcept {
        constexpr std::uint32_t FRAME_HEADER = 16u; // return address + saved RBP
        return static_cast<std::int32_t>(FRAME_HEADER + SHADOW_SPACE_BYTES + (arg_index - 4u) * SLOT_SIZE);
        // Loads from: [RBP + offset]
    }

    bool lower_formal_arguments_for_block(
        dgtl::machine::MachineFunction& function,
        dgtl::machine::BlockId block_id,
        std::span<const dgtl::machine::MachineRegister> param_vregs) const {
        using namespace dgtl::machine;
        static constexpr std::uint32_t kWin64GprArgs[4] = {1u, 2u, 8u, 9u}; // RCX, RDX, R8, R9
        if (param_vregs.empty() || function.blocks().empty()) return true;

        MachineBasicBlock* entry = function.block(block_id);
        if (!entry) return true;

        const InstrId cursor = entry->instructions.empty() ? INVALID_INSTR_ID : entry->instructions.front();

        for (std::size_t i = 0; i < param_vregs.size() && i < 4; ++i) {
            MachineRegister param_vreg = param_vregs[i];
            const bool is_fp = (param_vreg.reg_class == RegisterClassKind::FloatingPoint ||
                                param_vreg.reg_class == RegisterClassKind::Vector);
            const std::uint32_t reg_id = is_fp ? static_cast<std::uint32_t>(i) : kWin64GprArgs[i];
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
        }

        // Win64 stack parameters (5th parameter onwards, i >= 4)
        for (std::size_t i = 4; i < param_vregs.size(); ++i) {
            MachineRegister param_vreg = param_vregs[i];
            const std::int64_t disp = get_callee_stack_arg_offset(static_cast<std::uint32_t>(i));

            MachineInstruction& load_inst = function.create_instruction(GM_LOAD);
            load_inst.form = MachineForm::LoadStack;
            load_inst.semantic_kind = MachineSemanticKind::LoadStack;
            MachineMemoryOperand mem{};
            mem.base = MachineRegister{5u, RegisterKind::Physical, RegisterClassKind::Address}; // RBP
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
        static constexpr std::uint32_t kWin64GprArgs[4] = {1u, 2u, 8u, 9u};
        MachineInstruction* inst = function.instruction(call_instr);
        if (!inst) return false;

        std::vector<MachineOperand> arg_operands;
        for (const auto& op : inst->operands) {
            if (op.is_register() && op.is_use()) {
                arg_operands.push_back(op);
            }
        }

        for (std::size_t i = 0; i < arg_operands.size() && i < 4; ++i) {
            const bool is_fp = (arg_operands[i].reg.reg_class == RegisterClassKind::FloatingPoint ||
                                arg_operands[i].reg.reg_class == RegisterClassKind::Vector);
            const std::uint32_t reg_id = is_fp ? static_cast<std::uint32_t>(i) : kWin64GprArgs[i];
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
        }

        // Win64 outgoing stack parameters (5th parameter onwards, i >= 4)
        MachineRegister rsp{4u, RegisterKind::Physical, RegisterClassKind::Address};
        for (std::size_t i = 4; i < arg_operands.size(); ++i) {
            const std::int64_t disp = get_caller_stack_arg_offset(static_cast<std::uint32_t>(i));
            MachineInstruction& store_inst = function.create_instruction(GM_STORE);
            store_inst.form = MachineForm::StoreStack;
            store_inst.semantic_kind = MachineSemanticKind::StoreStack;
            MachineMemoryOperand mem{};
            mem.base = rsp;
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

        const std::uint32_t call_outgoing_bytes = static_cast<std::uint32_t>(
            std::max<std::size_t>(32u, arg_operands.size() * 8u));
        function.frame_info().has_calls = true;
        if (call_outgoing_bytes > function.frame_info().outgoing_argument_bytes) {
            function.frame_info().outgoing_argument_bytes = call_outgoing_bytes;
        }

        // Re-acquire instruction pointer: create_instruction may have reallocated instructions_ vector
        inst = function.instruction(call_instr);
        if (!inst) return false;

        static constexpr std::uint32_t kCallerSaved[7] = {0u, 1u, 2u, 8u, 9u, 10u, 11u};
        for (std::uint32_t reg_id : kCallerSaved) {
            MachineOperand clobber;
            clobber.kind = MachineOperandKind::Register;
            clobber.role = OperandRole::ImplicitDef;
            clobber.reg = MachineRegister::physical_reg(reg_id, RegisterClassKind::General);
            inst->add_operand(clobber);
        }
        for (std::uint32_t xmm_id = 0u; xmm_id < 6u; ++xmm_id) {
            MachineOperand clobber;
            clobber.kind = MachineOperandKind::Register;
            clobber.role = OperandRole::ImplicitDef;
            clobber.reg = MachineRegister::physical_reg(xmm_id, RegisterClassKind::FloatingPoint);
            inst->add_operand(clobber);
        }

        const MachineOperand* def_op = inst->get_def();
        if (def_op && def_op->reg.is_virtual()) {
            MachineRegister result_vreg = def_op->reg;
            MachineRegister phys_rax = MachineRegister::physical_reg(0u, result_vreg.reg_class);

            const InstrId next_cursor = inst->next_instr;
            MachineInstruction& copy_res = function.create_instruction(GM_COPY);
            copy_res.form = MachineForm::Move;
            copy_res.semantic_kind = MachineSemanticKind::Move;
            copy_res.add_operand(MachineOperand::def(result_vreg));
            copy_res.add_operand(MachineOperand::use(phys_rax));
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
                MachineRegister phys_rax = MachineRegister::physical_reg(0u, op.reg.reg_class);
                MachineInstruction& copy_inst = function.create_instruction(GM_COPY);
                copy_inst.form = MachineForm::Move;
                copy_inst.semantic_kind = MachineSemanticKind::Move;
                copy_inst.add_operand(MachineOperand::def(phys_rax));
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
        if (!lower_formal_arguments(function, function.params())) {
            return false;
        }

        // Lower formal parameters for any other subfunction entry blocks
        for (std::size_t b_idx = 0; b_idx < function.blocks().size(); ++b_idx) {
            MachineBasicBlock* bb = function.block(static_cast<BlockId>(b_idx));
            if (!bb) continue;
            if (bb->entry && b_idx != function.entry_block() && !bb->formal_params.empty()) {
                if (!lower_formal_arguments_for_block(function, bb->id, bb->formal_params)) {
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
                } else if (inst->semantic_kind == MachineSemanticKind::IntegerDiv ||
                           inst->semantic_kind == MachineSemanticKind::IntegerMod) {
                    static constexpr std::uint32_t kDivClobbers[3] = {0u, 2u, 11u}; // RAX, RDX, R11
                    for (std::uint32_t reg_id : kDivClobbers) {
                        MachineOperand clobber;
                        clobber.kind = MachineOperandKind::Register;
                        clobber.role = OperandRole::ImplicitDef;
                        clobber.reg = MachineRegister::physical_reg(reg_id, RegisterClassKind::General);
                        inst->add_operand(clobber);
                    }
                }
            }
        }
        return true;
    }
};

/**
 * @brief System V AMD64 Calling Convention ABI (Linux, BSD, macOS standard).
 * 6 GPR parameters (RDI, RSI, RDX, RCX, R8, R9), 8 FP parameters (XMM0-XMM7), 0 shadow space.
 */
class SystemVAMD64ABI final : public dgtl::machine::TargetABI {
public:
    [[nodiscard]] dgtl::machine::ABICallPlan classify_call(
        std::span<const dgtl::machine::ABIValue> arguments,
        std::optional<dgtl::machine::ABIValue> return_value) const override {

        using namespace dgtl::machine;

        ABICallPlan plan{};
        plan.stack_alignment = 16u;
        plan.shadow_space_bytes = 0u;

        // System V GPR arguments: RDI (7), RSI (6), RDX (2), RCX (1), R8 (8), R9 (9)
        static constexpr std::uint32_t kSysVGprArgs[6] = {7u, 6u, 2u, 1u, 8u, 9u};

        std::uint32_t next_gpr = 0;
        std::uint32_t next_fp = 0;
        std::uint32_t stack_offset = 0;

        for (const ABIValue& value : arguments) {
            const bool fp =
                value.kind == ABIValueKind::FloatingPoint ||
                value.kind == ABIValueKind::Vector ||
                value.preferred_class == RegisterClassKind::FloatingPoint ||
                value.preferred_class == RegisterClassKind::Vector;

            const std::uint32_t size = std::max<std::uint32_t>(value.size_bytes, 1u);
            const std::uint32_t aligned = (size + 7u) & ~7u;

            ABIArgLocation location{};
            location.size_bytes = size;

            if (fp && next_fp < 8u) {
                location.kind = ABIArgLocationKind::Register;
                location.reg = {
                    value.preferred_class == RegisterClassKind::Vector
                        ? (16u + next_fp) // ZMM0-ZMM7
                        : next_fp,        // XMM0-XMM7
                    RegisterKind::Physical,
                    value.preferred_class == RegisterClassKind::Vector
                        ? RegisterClassKind::Vector
                        : RegisterClassKind::FloatingPoint
                };
                ++next_fp;
            } else if (!fp && size <= 8u && next_gpr < 6u) {
                location.kind = ABIArgLocationKind::Register;
                location.reg = {
                    kSysVGprArgs[next_gpr],
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

        plan.stack_bytes = (stack_offset + 15u) & ~15u;

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
                        value.preferred_class == RegisterClassKind::Vector ? 16u : 0u,
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
                        0u, // RAX
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
        if (reg.reg_class == RegisterClassKind::FloatingPoint ||
            reg.reg_class == RegisterClassKind::Vector) {
            return true; // All XMM/ZMM are caller-saved in System V
        }
        // System V caller-saved: RAX=0, RDX=2, RCX=1, RSI=6, RDI=7, R8=8, R9=9, R10=10, R11=11
        return reg.id == 0u || reg.id == 1u || reg.id == 2u || reg.id == 6u ||
               reg.id == 7u || reg.id == 8u || reg.id == 9u || reg.id == 10u || reg.id == 11u;
    }

    [[nodiscard]] bool is_callee_saved(
        dgtl::machine::MachineRegister reg) const noexcept override {
        using namespace dgtl::machine;
        if (!reg.is_physical()) return false;
        if (reg.reg_class == RegisterClassKind::FloatingPoint ||
            reg.reg_class == RegisterClassKind::Vector) {
            return false;
        }
        // System V callee-saved: RBX=3, RSP=4, RBP=5, R12=12, R13=13, R14=14, R15=15
        return reg.id == 3u || reg.id == 4u || reg.id == 5u ||
               reg.id == 12u || reg.id == 13u || reg.id == 14u || reg.id == 15u;
    }

    [[nodiscard]] dgtl::machine::MachineRegister
    stack_pointer_register() const noexcept override {
        return {4u, dgtl::machine::RegisterKind::Physical, dgtl::machine::RegisterClassKind::Address}; // RSP
    }

    [[nodiscard]] dgtl::machine::MachineRegister
    frame_pointer_register() const noexcept override {
        return {5u, dgtl::machine::RegisterKind::Physical, dgtl::machine::RegisterClassKind::Address}; // RBP
    }

    [[nodiscard]] static constexpr std::int32_t get_systemv_caller_stack_arg_offset(std::uint32_t arg_index) noexcept {
        constexpr std::uint32_t SLOT_SIZE = 8u;
        return static_cast<std::int32_t>((arg_index - 6u) * SLOT_SIZE);
    }

    [[nodiscard]] static constexpr std::int32_t get_systemv_callee_stack_arg_offset(std::uint32_t arg_index) noexcept {
        constexpr std::uint32_t FRAME_HEADER = 16u; // return address + saved RBP
        constexpr std::uint32_t SLOT_SIZE = 8u;
        return static_cast<std::int32_t>(FRAME_HEADER + (arg_index - 6u) * SLOT_SIZE);
    }

    bool lower_formal_arguments_for_block(
        dgtl::machine::MachineFunction& function,
        dgtl::machine::BlockId block_id,
        std::span<const dgtl::machine::MachineRegister> param_vregs) const {
        using namespace dgtl::machine;
        static constexpr std::uint32_t kSysVGprArgs[6] = {7u, 6u, 2u, 1u, 8u, 9u}; // RDI, RSI, RDX, RCX, R8, R9
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
                const std::uint32_t reg_id = static_cast<std::uint32_t>(fp_idx++);
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
            } else if (!is_fp && gpr_idx < 6u) {
                const std::uint32_t reg_id = kSysVGprArgs[gpr_idx++];
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
                // Stack parameter (i >= 6 for GPRs / i >= 8 for FPRs)
                const std::uint32_t stack_idx = static_cast<std::uint32_t>(is_fp ? (fp_idx++) : (gpr_idx++));
                const std::int64_t disp = get_systemv_callee_stack_arg_offset(stack_idx);

                MachineInstruction& load_inst = function.create_instruction(GM_LOAD);
                load_inst.form = MachineForm::LoadStack;
                load_inst.semantic_kind = MachineSemanticKind::LoadStack;
                MachineMemoryOperand mem{};
                mem.base = MachineRegister{5u, RegisterKind::Physical, RegisterClassKind::Address}; // RBP
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
        static constexpr std::uint32_t kSysVGprArgs[6] = {7u, 6u, 2u, 1u, 8u, 9u};
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
        MachineRegister rsp{4u, RegisterKind::Physical, RegisterClassKind::Address};

        for (std::size_t i = 0; i < arg_operands.size(); ++i) {
            const bool is_fp = (arg_operands[i].reg.reg_class == RegisterClassKind::FloatingPoint ||
                                arg_operands[i].reg.reg_class == RegisterClassKind::Vector);

            if (is_fp && fp_idx < 8u) {
                const std::uint32_t reg_id = static_cast<std::uint32_t>(fp_idx++);
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
            } else if (!is_fp && gpr_idx < 6u) {
                const std::uint32_t reg_id = kSysVGprArgs[gpr_idx++];
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
                const std::int64_t disp = get_systemv_caller_stack_arg_offset(stack_idx);
                MachineInstruction& store_inst = function.create_instruction(GM_STORE);
                store_inst.form = MachineForm::StoreStack;
                store_inst.semantic_kind = MachineSemanticKind::StoreStack;
                MachineMemoryOperand mem{};
                mem.base = rsp;
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
            (gpr_idx > 6u ? (gpr_idx - 6u) : 0u) + (fp_idx > 8u ? (fp_idx - 8u) : 0u));
        const std::uint32_t call_outgoing_bytes = stack_args * 8u;
        function.frame_info().has_calls = true;
        if (call_outgoing_bytes > function.frame_info().outgoing_argument_bytes) {
            function.frame_info().outgoing_argument_bytes = call_outgoing_bytes;
        }

        inst = function.instruction(call_instr);
        if (!inst) return false;

        static constexpr std::uint32_t kSysVCallerSaved[9] = {0u, 1u, 2u, 6u, 7u, 8u, 9u, 10u, 11u};
        for (std::uint32_t reg_id : kSysVCallerSaved) {
            MachineOperand clobber;
            clobber.kind = MachineOperandKind::Register;
            clobber.role = OperandRole::ImplicitDef;
            clobber.reg = MachineRegister::physical_reg(reg_id, RegisterClassKind::General);
            inst->add_operand(clobber);
        }
        for (std::uint32_t xmm_id = 0u; xmm_id < 16u; ++xmm_id) {
            MachineOperand clobber;
            clobber.kind = MachineOperandKind::Register;
            clobber.role = OperandRole::ImplicitDef;
            clobber.reg = MachineRegister::physical_reg(xmm_id, RegisterClassKind::FloatingPoint);
            inst->add_operand(clobber);
        }

        const MachineOperand* def_op = inst->get_def();
        if (def_op && def_op->reg.is_virtual()) {
            MachineRegister result_vreg = def_op->reg;
            MachineRegister phys_ret = MachineRegister::physical_reg(0u, result_vreg.reg_class);

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
                MachineRegister phys_ret = MachineRegister::physical_reg(0u, op.reg.reg_class);
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
                } else if (inst->semantic_kind == MachineSemanticKind::IntegerDiv ||
                           inst->semantic_kind == MachineSemanticKind::IntegerMod) {
                    static constexpr std::uint32_t kDivClobbers[3] = {0u, 2u, 11u};
                    for (std::uint32_t reg_id : kDivClobbers) {
                        MachineOperand clobber;
                        clobber.kind = MachineOperandKind::Register;
                        clobber.role = OperandRole::ImplicitDef;
                        clobber.reg = MachineRegister::physical_reg(reg_id, RegisterClassKind::General);
                        inst->add_operand(clobber);
                    }
                }
            }
        }
        return true;
    }
};

} // namespace dgtl::isa::x64

export namespace dgtl::isa {
    using x64::X64FrameInfo;
    using x64::Win64ABI;
    using x64::SystemVAMD64ABI;
}
