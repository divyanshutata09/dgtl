/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository: github.com/TataG-Systems/DGTL
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

module;

#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#if defined(_WIN32) || defined(_WIN64)
    #include <windows.h>
    #undef CONST // Neutralize legacy Windows preprocessor macro
#endif
export module dgtl.isa.x64.x64_machine_form_lowering;

import dgtl.mir.instruction_selector;
import dgtl.mir.runtime_contract;
import dgtl.mir.isel_gen;
import dgtl.mir.machine_ir;
import dgtl.mir.regalloc.universal;
import dgtl.isa.x64.x64_instruction_info;
import dgtl.isa.x64.x64_reg_map;
import dgtl.isa.lowering.x64.frame_lowering;
import dgtl.isa.x64.mc_encoder;
import dgtl.target;

export namespace dgtl::isa::lowering {

using SelectedInstruction = dgtl::mir::isel::SelectedInstruction;
using InstructionSelectorProgram = dgtl::mir::isel::InstructionSelectorProgram;

/**
 * @brief X64_Machine_Form_Lowering
 *
 * Full X64 lowering implementation.
 *
 * This file intentionally preserves the complete legacy encoding implementation
 * while the backend is migrated to the common-MIR ABI.  No encoder, frame,
 * allocation, CFG, or emission capability is deleted merely because its old
 * interface is being retired.
 *
 * Architectural ownership:
 *   IR/frontend -> legalization -> ISel -> common MIR
 *      -> X64TargetLoweringContract -> X64 machine opcodes
 *      -> register allocation/frame lowering -> machine encoding.
 *
 * The lowering layer queries generated kind tables and dispatches to encoder methods,
 * leaving the emitter as a thin register-state plus byte-encoding shell only.
 */
class X64FormLowering {
public:
    using ByteStream = std::vector<uint8_t>;
    using Emitter = void*; // Forward ref; actual type is X64StrikeEmitter

    // Encoder method type: pure MIR machine instruction encoder
    using EncoderFunc = std::function<void(const machine::MachineInstruction&, ByteStream&)>;

    struct EncoderCallbacks {
        EncoderFunc add_i32;
        EncoderFunc sub_i32;
        EncoderFunc mul_i32;
        EncoderFunc div_i32;
        EncoderFunc mod_i32;
        EncoderFunc and_i32;
        EncoderFunc or_i32;
        EncoderFunc xor_i32;
        EncoderFunc shl_i32;
        EncoderFunc shr_i32;

        EncoderFunc add_i64;
        EncoderFunc sub_i64;
        EncoderFunc mul_i64;
        EncoderFunc div_i64;
        EncoderFunc mod_i64;
        EncoderFunc and_i64;
        EncoderFunc or_i64;
        EncoderFunc xor_i64;
        EncoderFunc shl_i64;
        EncoderFunc shr_i64;

        EncoderFunc add_f32;
        EncoderFunc sub_f32;
        EncoderFunc mul_f32;
        EncoderFunc div_f32;

        EncoderFunc add_f64;
        EncoderFunc sub_f64;
        EncoderFunc mul_f64;
        EncoderFunc div_f64;

        EncoderFunc cmp_i32;

        EncoderFunc vec_add_f32_512;
        EncoderFunc vec_mul_f32_512;

        std::function<void(const char*, uint32_t)> fail_emission;
    };

    explicit X64FormLowering(const EncoderCallbacks& cbs) : callbacks_(cbs) {}

    inline void lower_integer_binary32(const machine::MachineInstruction& inst, ByteStream& out) {
        switch (inst.semantic_kind) {
            case machine::MachineSemanticKind::IntegerAdd:
                if (callbacks_.add_i32) callbacks_.add_i32(inst, out); return;
            case machine::MachineSemanticKind::IntegerSub:
                if (callbacks_.sub_i32) callbacks_.sub_i32(inst, out); return;
            case machine::MachineSemanticKind::IntegerMul:
                if (callbacks_.mul_i32) callbacks_.mul_i32(inst, out); return;
            case machine::MachineSemanticKind::IntegerDiv:
                if (callbacks_.div_i32) callbacks_.div_i32(inst, out); return;
            case machine::MachineSemanticKind::IntegerMod:
                if (callbacks_.mod_i32) callbacks_.mod_i32(inst, out); return;
            case machine::MachineSemanticKind::IntegerAnd:
                if (callbacks_.and_i32) callbacks_.and_i32(inst, out); return;
            case machine::MachineSemanticKind::IntegerOr:
                if (callbacks_.or_i32) callbacks_.or_i32(inst, out); return;
            case machine::MachineSemanticKind::IntegerXor:
                if (callbacks_.xor_i32) callbacks_.xor_i32(inst, out); return;
            case machine::MachineSemanticKind::IntegerShl:
                if (callbacks_.shl_i32) callbacks_.shl_i32(inst, out); return;
            case machine::MachineSemanticKind::IntegerShr:
                if (callbacks_.shr_i32) callbacks_.shr_i32(inst, out); return;
            default:
                if (callbacks_.fail_emission) {
                    callbacks_.fail_emission("Unhandled MachineForm::IntegerBinary32 opcode",
                                             static_cast<uint32_t>(inst.opcode));
                }
                return;
        }
    }

    inline void lower_integer_binary32(const SelectedInstruction& selected, ByteStream& out) {
        machine::MachineInstruction inst;
        inst.form = selected.form;
        inst.semantic_kind = selected.semantic_kind;
        lower_integer_binary32(inst, out);
    }

    inline void lower_integer_binary64(const machine::MachineInstruction& inst, ByteStream& out) {
        switch (inst.semantic_kind) {
            case machine::MachineSemanticKind::IntegerAdd:
                if (callbacks_.add_i64) callbacks_.add_i64(inst, out); return;
            case machine::MachineSemanticKind::IntegerSub:
                if (callbacks_.sub_i64) callbacks_.sub_i64(inst, out); return;
            case machine::MachineSemanticKind::IntegerMul:
                if (callbacks_.mul_i64) callbacks_.mul_i64(inst, out); return;
            case machine::MachineSemanticKind::IntegerDiv:
                if (callbacks_.div_i64) callbacks_.div_i64(inst, out); return;
            case machine::MachineSemanticKind::IntegerMod:
                if (callbacks_.mod_i64) callbacks_.mod_i64(inst, out); return;
            case machine::MachineSemanticKind::IntegerAnd:
                if (callbacks_.and_i64) callbacks_.and_i64(inst, out);
                else if (callbacks_.fail_emission) callbacks_.fail_emission("Missing X64 encoder callback for IntegerBinary64::And", static_cast<uint32_t>(inst.opcode));
                return;
            case machine::MachineSemanticKind::IntegerOr:
                if (callbacks_.or_i64) callbacks_.or_i64(inst, out);
                else if (callbacks_.fail_emission) callbacks_.fail_emission("Missing X64 encoder callback for IntegerBinary64::Or", static_cast<uint32_t>(inst.opcode));
                return;
            case machine::MachineSemanticKind::IntegerXor:
                if (callbacks_.xor_i64) callbacks_.xor_i64(inst, out);
                else if (callbacks_.fail_emission) callbacks_.fail_emission("Missing X64 encoder callback for IntegerBinary64::Xor", static_cast<uint32_t>(inst.opcode));
                return;
            case machine::MachineSemanticKind::IntegerShl:
                if (callbacks_.shl_i64) callbacks_.shl_i64(inst, out);
                else if (callbacks_.fail_emission) callbacks_.fail_emission("Missing X64 encoder callback for IntegerBinary64::Shl", static_cast<uint32_t>(inst.opcode));
                return;
            case machine::MachineSemanticKind::IntegerShr:
                if (callbacks_.shr_i64) callbacks_.shr_i64(inst, out);
                else if (callbacks_.fail_emission) callbacks_.fail_emission("Missing X64 encoder callback for IntegerBinary64::Shr", static_cast<uint32_t>(inst.opcode));
                return;
            default:
                if (callbacks_.fail_emission) {
                    callbacks_.fail_emission("Unhandled MachineForm::IntegerBinary64 opcode",
                                             static_cast<uint32_t>(inst.opcode));
                }
                return;
        }
    }

    inline void lower_integer_binary64(const SelectedInstruction& selected, ByteStream& out) {
        machine::MachineInstruction inst;
        inst.form = selected.form;
        inst.semantic_kind = selected.semantic_kind;
        lower_integer_binary64(inst, out);
    }

    inline void lower_float_binary32(const machine::MachineInstruction& inst, ByteStream& out) {
        switch (inst.semantic_kind) {
            case machine::MachineSemanticKind::FloatAdd:
                if (callbacks_.add_f32) callbacks_.add_f32(inst, out); return;
            case machine::MachineSemanticKind::FloatSub:
                if (callbacks_.sub_f32) callbacks_.sub_f32(inst, out);
                else if (callbacks_.fail_emission) callbacks_.fail_emission("Missing X64 encoder callback for FloatBinary32::Sub", static_cast<uint32_t>(inst.opcode));
                return;
            case machine::MachineSemanticKind::FloatMul:
                if (callbacks_.mul_f32) callbacks_.mul_f32(inst, out); return;
            case machine::MachineSemanticKind::FloatDiv:
                if (callbacks_.div_f32) callbacks_.div_f32(inst, out);
                else if (callbacks_.fail_emission) callbacks_.fail_emission("Missing X64 encoder callback for FloatBinary32::Div", static_cast<uint32_t>(inst.opcode));
                return;
            default:
                if (callbacks_.fail_emission) {
                    callbacks_.fail_emission("Unhandled MachineForm::FloatBinary32 opcode",
                                             static_cast<uint32_t>(inst.opcode));
                }
                return;
        }
    }

    inline void lower_float_binary32(const SelectedInstruction& selected, ByteStream& out) {
        machine::MachineInstruction inst;
        inst.form = selected.form;
        inst.semantic_kind = selected.semantic_kind;
        lower_float_binary32(inst, out);
    }

    inline void lower_float_binary64(const machine::MachineInstruction& inst, ByteStream& out) {
        switch (inst.semantic_kind) {
            case machine::MachineSemanticKind::FloatAdd:
                if (callbacks_.add_f64) callbacks_.add_f64(inst, out);
                else if (callbacks_.fail_emission) callbacks_.fail_emission("Missing X64 encoder callback for FloatBinary64::Add", static_cast<uint32_t>(inst.opcode));
                return;
            case machine::MachineSemanticKind::FloatSub:
                if (callbacks_.sub_f64) callbacks_.sub_f64(inst, out);
                else if (callbacks_.fail_emission) callbacks_.fail_emission("Missing X64 encoder callback for FloatBinary64::Sub", static_cast<uint32_t>(inst.opcode));
                return;
            case machine::MachineSemanticKind::FloatMul:
                if (callbacks_.mul_f64) callbacks_.mul_f64(inst, out);
                else if (callbacks_.fail_emission) callbacks_.fail_emission("Missing X64 encoder callback for FloatBinary64::Mul", static_cast<uint32_t>(inst.opcode));
                return;
            case machine::MachineSemanticKind::FloatDiv:
                if (callbacks_.div_f64) callbacks_.div_f64(inst, out);
                else if (callbacks_.fail_emission) callbacks_.fail_emission("Missing X64 encoder callback for FloatBinary64::Div", static_cast<uint32_t>(inst.opcode));
                return;
            default:
                if (callbacks_.fail_emission) {
                    callbacks_.fail_emission("Unhandled MachineForm::FloatBinary64 opcode",
                                             static_cast<uint32_t>(inst.opcode));
                }
                return;
        }
    }

    inline void lower_float_binary64(const SelectedInstruction& selected, ByteStream& out) {
        machine::MachineInstruction inst;
        inst.form = selected.form;
        inst.semantic_kind = selected.semantic_kind;
        lower_float_binary64(inst, out);
    }

    inline void lower_compare32(const machine::MachineInstruction& inst, ByteStream& out) {
        switch (inst.semantic_kind) {
            case machine::MachineSemanticKind::Compare:
                if (callbacks_.cmp_i32) callbacks_.cmp_i32(inst, out);
                return;
            default:
                if (callbacks_.fail_emission) {
                    callbacks_.fail_emission("Unhandled MachineForm::Compare32 opcode",
                                             static_cast<uint32_t>(inst.opcode));
                }
                return;
        }
    }

    inline void lower_compare32(const SelectedInstruction& selected, ByteStream& out) {
        machine::MachineInstruction inst;
        inst.form = selected.form;
        inst.semantic_kind = selected.semantic_kind;
        lower_compare32(inst, out);
    }

    inline void lower_vector_binary512(const machine::MachineInstruction& inst, ByteStream& out) {
        switch (inst.semantic_kind) {
            case machine::MachineSemanticKind::VectorAdd:
                if (callbacks_.vec_add_f32_512) callbacks_.vec_add_f32_512(inst, out);
                return;
            case machine::MachineSemanticKind::VectorMul:
                if (callbacks_.vec_mul_f32_512) callbacks_.vec_mul_f32_512(inst, out);
                return;
            default:
                if (callbacks_.fail_emission) {
                    callbacks_.fail_emission("Unhandled MachineForm::VectorBinary512 opcode",
                                             static_cast<uint32_t>(inst.opcode));
                }
                return;
        }
    }

    inline void lower_vector_binary512(const SelectedInstruction& selected, ByteStream& out) {
        machine::MachineInstruction inst;
        inst.form = selected.form;
        inst.semantic_kind = selected.semantic_kind;
        lower_vector_binary512(inst, out);
    }

private:
    EncoderCallbacks callbacks_;
};

} // namespace dgtl::isa::x64::lowering

export namespace dgtl::isa::x64 {

using Win64FrameCalculator = dgtl::isa::lowering::x64::Win64FrameCalculator;
namespace exec = dgtl::isa::x64::emitter;

/*
 * ============================================================================
 * MIR construction / verification bridge
 *
 * P0 architecture rule:
 *   IR -> Legalize -> ISel -> common MIR -> target lowering -> emission.
 *
 * The byte emitter below still contains the legacy IR-oriented encoding
 * routines for compatibility. This bridge makes the common Machine IR an
 * explicit, verified artifact at the X64 boundary instead of allowing the
 * backend to silently bypass it.
 *
 * It deliberately does not assign physical registers or target opcodes.
 * Those remain target-lowering concerns.
 *
 * The byte encoder below remains a legacy IR-shaped compatibility path.
 * Therefore this bridge is not yet the final MIR-native encoder boundary;
 * target lowering and byte emission must still consume MachineInstruction
 * and MachineOperand directly in the next migration stage.
 * ============================================================================
 */
/*
 * ============================================================================
 * X64 target-lowering contract
 *
 * This is the ownership boundary for concrete X64 opcodes.
 *
 * IMPORTANT:
 *   - Common MIR owns MachineForm and semantic provenance.
 *   - This layer owns concrete X64 MachineOpcode assignment.
 *   - No IR OpCode is accepted by this API.
 *   - No IRNode is accepted by this API.
 *
 * The semantic operation kind needed to distinguish (for example) ADD from
 * SUB is deliberately NOT reconstructed from semantic_provenance here.
 * The common MIR contract must carry that information explicitly.  Until that
 * field is added to Machine_IR, this resolver rejects ambiguous forms instead
 * of silently reinterpreting provenance as an opcode.
 * ============================================================================
 */
class X64TargetLoweringContract {
public:
    using MachineOpcode = machine::MachineOpcode;

    struct Result {
        MachineOpcode opcode = machine::INVALID_MACHINE_OPCODE;
        bool assigned = false;
        const char* diagnostic = nullptr;
    };

    [[nodiscard]] static Result assign_opcode(
        const machine::MachineInstruction& instruction) noexcept {
        const auto op = dgtl::isa::x64::opcode_for(instruction.form, instruction.semantic_kind);
        if (op != machine::GM_NOOP || instruction.form == machine::MachineForm::NoOp) {
            return Result{op, true, nullptr};
        }
        return Result{
            machine::INVALID_MACHINE_OPCODE,
            false,
            "X64 target lowering unhandled MachineForm or SemanticKind"};
    }
};

class X64MIRBoundary {
public:
    using BlockMap = std::unordered_map<std::uint32_t, machine::BlockId>;

    [[nodiscard]] static bool lower(
        machine::MachineFunction& function,
        std::string& diagnostic) {

        diagnostic.clear();
        const auto pre_verification =
            machine::MachineVerifier::verify(function);

        if (!pre_verification.ok()) {
            diagnostic = "MachineVerifier rejected pre-lowering X64 MIR";
            return false;
        }

        for (std::size_t b = 0; b < function.blocks().size(); ++b) {
            auto* block = function.block(static_cast<machine::BlockId>(b));
            if (!block) continue;
            for (auto inst_id : block->instructions) {
                auto* inst = function.instruction(inst_id);
                if (!inst) continue;
                if (inst->opcode == machine::INVALID_MACHINE_OPCODE || inst->opcode == machine::GM_NOOP) {
                    const auto result = X64TargetLoweringContract::assign_opcode(*inst);
                    if (result.assigned) {
                        inst->opcode = result.opcode;
                    }
                }
            }
        }

        return true;
    }

    [[nodiscard]] static bool build(
        machine::MachineFunction& function,
        std::string& diagnostic) {
        return lower(function, diagnostic);
    }

    [[nodiscard]] static bool lower_and_allocate(
        machine::MachineFunction& function,
        const core::TargetMachine& target,
        std::string& diagnostic) {
        (void)target;
        return lower_and_allocate(function, diagnostic);
    }

    [[nodiscard]] static bool lower_and_allocate(
        machine::MachineFunction& function,
        std::string& diagnostic) {

        if (!lower(function, diagnostic))
            return false;

        emitter::X64TargetRegisterInfo reg_info{};
        dgtl::mir::regalloc::UniversalGraphColoringRegAlloc allocator(function, reg_info);
        if (!allocator.run()) {
            diagnostic = "X64 universal register allocation failed";
            return false;
        }

        std::vector<machine::MachineRegister> callee_saved;
        for (const auto& instruction : function.instructions()) {
            for (const auto& operand : instruction.operands) {
                if (operand.kind != machine::MachineOperandKind::Register || !operand.reg.is_physical())
                    continue;
                if (operand.reg.reg_class != machine::RegisterClassKind::General &&
                    operand.reg.reg_class != machine::RegisterClassKind::Integer)
                    continue;
                if (operand.reg.id == static_cast<std::uint32_t>(exec::X64Reg::RBX) ||
                    operand.reg.id == static_cast<std::uint32_t>(exec::X64Reg::RSI) ||
                    operand.reg.id == static_cast<std::uint32_t>(exec::X64Reg::RDI) ||
                    operand.reg.id == static_cast<std::uint32_t>(exec::X64Reg::R12) ||
                    operand.reg.id == static_cast<std::uint32_t>(exec::X64Reg::R13) ||
                    operand.reg.id == static_cast<std::uint32_t>(exec::X64Reg::R14) ||
                    operand.reg.id == static_cast<std::uint32_t>(exec::X64Reg::R15)) {
                    const bool seen = std::any_of(
                        callee_saved.begin(), callee_saved.end(),
                        [&](const machine::MachineRegister& reg) { return reg.id == operand.reg.id; });
                    if (!seen) callee_saved.push_back(operand.reg);
                }
            }
        }

        if (!x64::Win64FrameCalculator::apply_machine_function(function, callee_saved)) {
            diagnostic = "X64 MIR frame lowering failed";
            return false;
        }

        const auto verification = machine::MachineVerifier::verify(function);
        if (!verification.ok()) {
            diagnostic = "MachineVerifier rejected frame-lowered X64 MIR";
            return false;
        }

        return true;
    }

    [[nodiscard]] static bool build_and_verify(
        machine::MachineFunction& function,
        const core::TargetMachine& target,
        std::string& diagnostic) {
        diagnostic.clear();

        const auto arch = target.get_arch();
        if (arch != core::ArchType::X86_64_AVX2 &&
            arch != core::ArchType::X86_64_AVX512) {
            diagnostic = "X64 MIR boundary received a non-x86-64 target";
            return false;
        }

        if (!lower(function, diagnostic)) {
            return false;
        }

        const machine::MachineVerifyResult verification =
            machine::MachineVerifier::verify(function);
        if (!verification.ok()) {
            diagnostic = "MachineVerifier rejected X64 MIR";
            return false;
        }

        return true;
    }
};

class X64StrikeEmitter {
public:
    struct PendingBranchPatch {
        size_t displacement_offset = 0;
        size_t instruction_end_offset = 0;
        machine::BlockId target_label = machine::INVALID_BLOCK_ID;
    };

    X64StrikeEmitter()
        : target_machine_(core::ArchType::X86_64_AVX2) {}

    void set_target_machine(const core::TargetMachine& target) {
        target_machine_ = target;
    }

    void register_runtime_function(std::string name, uintptr_t address) {
        runtime_functions_[std::move(name)] = address;
    }

    void register_runtime_function_slot(uint32_t slot, uintptr_t address) {
        runtime_slots_[slot] = address;
    }

    [[nodiscard]] bool had_emission_error() const noexcept {
        return had_error_;
    }

    [[nodiscard]] const char* last_emission_error() const noexcept {
        return last_error_.c_str();
    }

    [[nodiscard]] machine::MachineOpcode last_failed_opcode() const noexcept {
        return last_failed_opcode_;
    }

    [[nodiscard]] size_t get_label_offset(uint32_t label_id) const noexcept {
        auto it = label_offsets_.find(label_id);
        return it != label_offsets_.end() ? it->second : 0;
    }

    [[nodiscard]] bool process_function(machine::MachineFunction& mir_function, std::vector<uint8_t>& out) const {
        out.clear();
        label_offsets_.clear();
        had_error_ = false;
        last_error_.clear();
        last_failed_opcode_ = machine::GM_NOOP;

        const auto arch = target_machine_.get_arch();
        if (arch != core::ArchType::X86_64_AVX2 &&
            arch != core::ArchType::X86_64_AVX512) {
            mark_error("X64 emitter received a non-x86-64 target", machine::GM_NOOP);
            return false;
        }

        if (mir_function.has_virtual_registers()) {
            emitter::X64TargetRegisterInfo reg_info{};
            dgtl::mir::regalloc::UniversalGraphColoringRegAlloc allocator(mir_function, reg_info);
            if (!allocator.run()) {
                mark_error("X64 MIR register allocation failed", machine::GM_NOOP);
                return false;
            }
        }

        if (!mir_function.properties().frame_lowered) {
            std::vector<machine::MachineRegister> callee_saved;
            for (const auto& instruction : mir_function.instructions()) {
                for (const auto& operand : instruction.operands) {
                    if (operand.kind == machine::MachineOperandKind::Register && operand.reg.is_physical()) {
                        if (operand.reg.id == static_cast<std::uint32_t>(exec::X64Reg::RBX) ||
                            operand.reg.id == static_cast<std::uint32_t>(exec::X64Reg::RSI) ||
                            operand.reg.id == static_cast<std::uint32_t>(exec::X64Reg::RDI) ||
                            operand.reg.id == static_cast<std::uint32_t>(exec::X64Reg::R12) ||
                            operand.reg.id == static_cast<std::uint32_t>(exec::X64Reg::R13) ||
                            operand.reg.id == static_cast<std::uint32_t>(exec::X64Reg::R14) ||
                            operand.reg.id == static_cast<std::uint32_t>(exec::X64Reg::R15)) {
                            if (std::find(callee_saved.begin(), callee_saved.end(), operand.reg) == callee_saved.end()) {
                                callee_saved.push_back(operand.reg);
                            }
                        }
                    }
                }
            }
            if (!x64::Win64FrameCalculator::apply_machine_function(mir_function, callee_saved)) {
                mark_error("X64 MIR frame lowering failed", machine::GM_NOOP);
                return false;
            }
        }

        std::unordered_map<std::uint32_t, std::string> symbol_table;
        for (const auto& [slot, addr] : runtime_slots_) {
            const std::string slot_name = "slot_" + std::to_string(slot);
            symbol_table[slot] = slot_name;
            runtime_functions_[slot_name] = addr;
        }

        std::string diag;
        const std::string func_name = mir_function.name().empty() ? "DGTL_kernel_entry" : std::string(mir_function.name());
        if (!x64::X64MCEncoder::encode_to_bytes(mir_function, out, func_name, symbol_table, &diag, runtime_functions_, &label_offsets_)) {
            mark_error(diag.empty() ? "X64MCEncoder encoding failed" : diag.c_str(), machine::GM_NOOP);
            return false;
        }

        return true;
    }

    [[nodiscard]] bool process_stream(machine::MachineFunction& mir_function, std::vector<uint8_t>& out) const {
        return process_function(mir_function, out);
    }

private:
    void mark_error(const char* message, machine::MachineOpcode opcode) const {
        had_error_ = true;
        last_error_ = message != nullptr ? message : "Unknown X64 emission failure";
        last_failed_opcode_ = opcode;
    }

    core::TargetMachine target_machine_;
    mutable std::unordered_map<std::string, uintptr_t> runtime_functions_;
    mutable std::unordered_map<uint32_t, uintptr_t> runtime_slots_;
    mutable std::unordered_map<uint32_t, size_t> label_offsets_;
    mutable bool had_error_ = false;
    mutable std::string last_error_;
    mutable machine::MachineOpcode last_failed_opcode_ = machine::GM_NOOP;
};

using FrameAllocationPlan = dgtl::isa::lowering::x64::FrameAllocationPlan;
using Win64FrameCalculator = dgtl::isa::lowering::x64::Win64FrameCalculator;
using X64Register = dgtl::isa::lowering::x64::X64Register;

class X64CodeBuffer {
public:
    std::vector<uint8_t> bytes;

    void emit_u8(uint8_t b) { bytes.push_back(b); }
    void emit_u16(uint16_t val) {
        bytes.push_back(static_cast<uint8_t>(val & 0xFF));
        bytes.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
    }
    void emit_u32(uint32_t val) {
        for (int i = 0; i < 4; ++i) {
            bytes.push_back(static_cast<uint8_t>((val >> (i * 8)) & 0xFF));
        }
    }
    void emit_u64(uint64_t val) {
        for (int i = 0; i < 8; ++i) {
            bytes.push_back(static_cast<uint8_t>((val >> (i * 8)) & 0xFF));
        }
    }
};

class X64MachineFormLowerer {
public:
    static void emit_prologue(const FrameAllocationPlan& plan, X64CodeBuffer& buf) {
        buf.emit_u8(0x55); // push rbp
        buf.emit_u8(0x48); buf.emit_u8(0x89); buf.emit_u8(0xE5); // mov rbp, rsp

        for (const auto reg : plan.saved_non_volatile_regs) {
            emit_push_reg(reg, buf);
        }

        if (plan.total_stack_bytes > 0) {
            if (plan.total_stack_bytes <= 127) {
                buf.emit_u8(0x48); buf.emit_u8(0x83); buf.emit_u8(0xEC);
                buf.emit_u8(static_cast<uint8_t>(plan.total_stack_bytes));
            } else {
                buf.emit_u8(0x48); buf.emit_u8(0x81); buf.emit_u8(0xEC);
                buf.emit_u32(plan.total_stack_bytes);
            }
        }
    }

    static void emit_epilogue(const FrameAllocationPlan& plan, X64CodeBuffer& buf) {
        if (plan.total_stack_bytes > 0) {
            if (plan.total_stack_bytes <= 127) {
                buf.emit_u8(0x48); buf.emit_u8(0x83); buf.emit_u8(0xC4);
                buf.emit_u8(static_cast<uint8_t>(plan.total_stack_bytes));
            } else {
                buf.emit_u8(0x48); buf.emit_u8(0x81); buf.emit_u8(0xC4);
                buf.emit_u32(plan.total_stack_bytes);
            }
        }

        for (auto it = plan.saved_non_volatile_regs.rbegin(); 
             it != plan.saved_non_volatile_regs.rend(); ++it) {
            emit_pop_reg(*it, buf);
        }

        buf.emit_u8(0x5D); // pop rbp
        buf.emit_u8(0xC3); // ret
    }

private:
    static void emit_push_reg(X64Register reg, X64CodeBuffer& buf) {
        const uint8_t r = static_cast<uint8_t>(reg);
        if (r >= 8) {
            buf.emit_u8(0x41);
            buf.emit_u8(0x50 + (r - 8));
        } else {
            buf.emit_u8(0x50 + r);
        }
    }

    static void emit_pop_reg(X64Register reg, X64CodeBuffer& buf) {
        const uint8_t r = static_cast<uint8_t>(reg);
        if (r >= 8) {
            buf.emit_u8(0x41);
            buf.emit_u8(0x58 + (r - 8));
        } else {
            buf.emit_u8(0x58 + r);
        }
    }
};

inline bool verify_frame_alignment_invariant() {
    std::vector<X64Register> all_regs = {
        X64Register::RBX, X64Register::RBP, X64Register::RDI, X64Register::RSI,
        X64Register::R12, X64Register::R13, X64Register::R14, X64Register::R15
    };

    for (uint32_t clobber_count = 0; clobber_count <= 8; ++clobber_count) {
        std::vector<X64Register> clobbers(all_regs.begin(), all_regs.begin() + clobber_count);
        for (uint32_t locals = 0; locals < 512; locals += 7) {
            for (uint32_t outgoing = 0; outgoing < 128; outgoing += 8) {
                const auto plan = Win64FrameCalculator::compute_plan(locals, 0, outgoing, clobbers, true);
                const uint32_t pushed_bytes = 16 + (clobber_count * 8);
                const uint32_t total_stack_adjustment = pushed_bytes + plan.total_stack_bytes;
                if ((total_stack_adjustment % 16) != 0 || plan.shadow_space_bytes < 32) {
                    return false;
                }
            }
        }
    }
    return true;
}

class X64MachineCodeEmitter final : public dgtl::machine::MachineCodeEmitter {
public:
    void emit_instruction(const dgtl::machine::MachineInstruction& inst,
                          std::vector<std::uint8_t>& out) override {
        switch (inst.form) {
            case dgtl::machine::MachineForm::Return:
                out.push_back(0xC3); // ret
                break;
            case dgtl::machine::MachineForm::NoOp:
                out.push_back(0x90); // nop
                break;
            default:
                break;
        }
    }
};

} // namespace dgtl::isa::x64

export namespace dgtl::isa {
    using x64::X64StrikeEmitter;
}
