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

    void register_runtime_function(const std::string& name, uintptr_t address) {
        runtime_functions_[name] = address;
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

        /*
         * Target register allocation is a strict MIR pass.
         */
        emitter::X64TargetRegisterInfo reg_info{};
        dgtl::mir::regalloc::UniversalGraphColoringRegAlloc allocator(mir_function, reg_info);
        if (!allocator.run()) {
            mark_error("X64 MIR register allocation failed", machine::GM_NOOP);
            return false;
        }

        /*
         * Frame lowering is likewise MIR-native.
         */
        std::vector<machine::MachineRegister> callee_saved;
        for (const auto& instruction : mir_function.instructions()) {
            for (const auto& operand : instruction.operands) {
                if (operand.kind != machine::MachineOperandKind::Register ||
                    !operand.reg.is_physical()) {
                    continue;
                }
                if (operand.reg.reg_class != machine::RegisterClassKind::General &&
                    operand.reg.reg_class != machine::RegisterClassKind::Integer) {
                    continue;
                }
                if (operand.reg.id == static_cast<std::uint32_t>(exec::X64Reg::RBX) ||
                    operand.reg.id == static_cast<std::uint32_t>(exec::X64Reg::R12) ||
                    operand.reg.id == static_cast<std::uint32_t>(exec::X64Reg::R13) ||
                    operand.reg.id == static_cast<std::uint32_t>(exec::X64Reg::R14) ||
                    operand.reg.id == static_cast<std::uint32_t>(exec::X64Reg::R15)) {
                    const bool seen = std::any_of(
                        callee_saved.begin(), callee_saved.end(),
                        [&](const machine::MachineRegister& reg) {
                            return reg.id == operand.reg.id;
                        });
                    if (!seen) callee_saved.push_back(operand.reg);
                }
            }
        }

        if (!x64::Win64FrameCalculator::apply_machine_function(
                mir_function, callee_saved)) {
            mark_error("X64 MIR frame lowering failed", machine::GM_NOOP);
            return false;
        }

        const uint32_t frame_size = mir_function.frame_info().total_frame_bytes;
        if (frame_size == 0u && mir_function.frame_info().has_calls) {
            mark_error("X64 MIR frame layout produced no call frame", machine::GM_NOOP);
            return false;
        }

        if (mir_function.has_virtual_registers()) {
            mark_error("X64 MIR emission received virtual registers after frame lowering",
                       machine::GM_NOOP);
            return false;
        }

        const auto post_lowering_verify =
            machine::MachineVerifier::verify(mir_function);
        if (!post_lowering_verify.ok()) {
            mark_error("MachineVerifier rejected allocated/frame-lowered X64 MIR",
                       machine::GM_NOOP);
            return false;
        }

        std::unordered_map<machine::BlockId, size_t> mir_block_offsets;
        std::vector<PendingBranchPatch> branch_patches;
        branch_patches.reserve(mir_function.instructions().size() / 8u + 4u);

        const bool mir_emitted = emit_mir_native_function(
            mir_function, frame_size, out, mir_block_offsets, branch_patches);
        if (!mir_emitted) {
            if (out.empty()) {
                out.push_back(0xC3);
            }
            return false;
        }

        // --- BACKPATCH MIR BLOCK BRANCHES ---
        for (const PendingBranchPatch& patch : branch_patches) {
            const auto target_it = mir_block_offsets.find(patch.target_label);
            if (target_it == mir_block_offsets.end()) {
                mark_error("Unresolved MIR basic-block branch target", machine::GM_GOTO);
                continue;
            }

            const int64_t target_offset = static_cast<int64_t>(target_it->second);
            const int64_t instr_end = static_cast<int64_t>(patch.instruction_end_offset);
            const int64_t rel64 = target_offset - instr_end;
            if (rel64 < static_cast<int64_t>(std::numeric_limits<int32_t>::min()) ||
                rel64 > static_cast<int64_t>(std::numeric_limits<int32_t>::max())) {
                mark_error("MIR branch target displacement out of range", machine::GM_GOTO);
                continue;
            }

            if (patch.displacement_offset + 4u > out.size()) {
                mark_error("Invalid MIR branch patch offset", machine::GM_GOTO);
                continue;
            }

            const int32_t rel32 = static_cast<int32_t>(rel64);
            out[patch.displacement_offset + 0u] = static_cast<uint8_t>((rel32 >> 0) & 0xFF);
            out[patch.displacement_offset + 1u] = static_cast<uint8_t>((rel32 >> 8) & 0xFF);
            out[patch.displacement_offset + 2u] = static_cast<uint8_t>((rel32 >> 16) & 0xFF);
            out[patch.displacement_offset + 3u] = static_cast<uint8_t>((rel32 >> 24) & 0xFF);
        }

        if (out.empty() || out.back() != 0xC3) {
            emit_add_rsp(out, frame_size);
            out.push_back(0xC3);
        }

        return !had_error_;
    }

    [[nodiscard]] bool process_stream(machine::MachineFunction& mir_function, std::vector<uint8_t>& out) const {
        return process_function(mir_function, out);
    }

private:
    static void append_u64(std::vector<uint8_t>& out, uint64_t value) {
        for (int i = 0; i < 8; ++i) {
            out.push_back(static_cast<uint8_t>((value >> (i * 8)) & 0xFFu));
        }
    }

    static void emit_modrm_reg_reg(std::vector<uint8_t>& out, uint8_t reg, uint8_t rm) {
        const uint8_t modrm = static_cast<uint8_t>(0xC0u | ((reg & 0x7u) << 3u) | (rm & 0x7u));
        out.push_back(modrm);
    }

    static void emit_mov_imm32_signext(std::vector<uint8_t>& out, uint8_t dst, uint32_t imm32) {
        out.push_back(exec::X64Encoding::rex(/*w=*/true,
                                             /*r=*/false,
                                             /*x=*/false,
                                             /*b=*/requires_rex(dst)));
        out.push_back(0xC7); // MOV r/m64, imm32 (sign-extended)
        emit_modrm_reg_reg(out, 0x00, dst);
        out.push_back(static_cast<uint8_t>(imm32 & 0xFFu));
        out.push_back(static_cast<uint8_t>((imm32 >> 8u) & 0xFFu));
        out.push_back(static_cast<uint8_t>((imm32 >> 16u) & 0xFFu));
        out.push_back(static_cast<uint8_t>((imm32 >> 24u) & 0xFFu));
    }

    static void emit_prefetch_t0_from_base(std::vector<uint8_t>& out, uint8_t base) {
        out.push_back(exec::X64Encoding::rex(/*w=*/false,
                                             /*r=*/false,
                                             /*x=*/false,
                                             /*b=*/requires_rex(base)));
        out.push_back(0x0F);
        out.push_back(0x18); // PREFETCHh m8

        const uint8_t base_lo = static_cast<uint8_t>(base & 0x7u);
        if (base_lo == 0x4u) {
            out.push_back(0x0Cu); // mod=00, reg=001 (T0), rm=100 (SIB follows)
            out.push_back(static_cast<uint8_t>(0x20u | base_lo));
            return;
        }
        if (base_lo == 0x5u) {
            out.push_back(0x4Du); // mod=01, reg=001 (T0), rm=101
            out.push_back(0x00u); // disp8 = 0
            return;
        }

        out.push_back(static_cast<uint8_t>(0x08u | base_lo));
    }

    static void emit_clflush_from_base(std::vector<uint8_t>& out, uint8_t base) {
        out.push_back(exec::X64Encoding::rex(/*w=*/false,
                                             /*r=*/false,
                                             /*x=*/false,
                                             /*b=*/requires_rex(base)));
        out.push_back(0x0F);
        out.push_back(0xAE); // CLFLUSH m8 with /7

        const uint8_t base_lo = static_cast<uint8_t>(base & 0x7u);
        if (base_lo == 0x4u) {
            out.push_back(0x3Cu);
            out.push_back(static_cast<uint8_t>(0x20u | base_lo));
            return;
        }
        if (base_lo == 0x5u) {
            out.push_back(0x7Du);
            out.push_back(0x00u);
            return;
        }

        out.push_back(static_cast<uint8_t>(0x38u | base_lo));
    }

    /*
     * The compatibility encoder uses signed 32-bit x86 displacement fields.
     * Keep virtual-value homes addressable without modulo aliasing.
     */
    static constexpr std::uint64_t MAX_ADDRESSABLE_VREG =
        (static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max()) - 40ull) / 8ull;

    /*
     * The old implementation used a fixed 131,112-byte frame and
     * `vreg % MAX_FRAME_SLOTS` addressing. That creates silent aliasing:
     * virtual registers separated by 16,384 names address the same stack
     * location. A backend must never trade correctness for a convenient
     * bounded hash.
     *
     * Until MachineFrameInfo stack slots become the sole source of offsets,
     * this compatibility path uses a deterministic one-to-one value-home
     * layout and computes the minimum frame required by the stream.
     */
    static uint32_t compute_frame_size(const machine::MachineFunction& function) noexcept {
        if (function.frame_info().total_frame_bytes > 0) {
            return function.frame_info().total_frame_bytes;
        }
        std::uint64_t max_vreg = 0u;
        for (const auto& inst : function.instructions()) {
            for (const auto& op : inst.operands) {
                if (op.kind == machine::MachineOperandKind::Register && op.reg.valid()) {
                    max_vreg = std::max<std::uint64_t>(max_vreg, static_cast<std::uint64_t>(op.reg.id));
                }
            }
        }
        const std::uint64_t required = 40ull + ((max_vreg + 1ull) * 8ull);
        const std::uint64_t aligned = (required + 15ull) & ~15ull;
        if (aligned > std::numeric_limits<std::uint32_t>::max()) {
            return 0u;
        }
        return static_cast<std::uint32_t>(aligned);
    }

    static int32_t vreg_offset(uint32_t vreg) noexcept {
        if (vreg == machine::INVALID_REG || vreg == 0u ||
            static_cast<std::uint64_t>(vreg) > MAX_ADDRESSABLE_VREG) {
            return 40;
        }

        const std::uint64_t offset =
            40ull + static_cast<std::uint64_t>(vreg) * 8ull;

        if (offset > static_cast<std::uint64_t>(
                         std::numeric_limits<std::int32_t>::max())) {
            return 40;
        }

        return static_cast<int32_t>(offset);
    }

    static void emit_sub_rsp(std::vector<uint8_t>& out, uint32_t amount) {
        if (amount == 0) return;
#if defined(_WIN32)
        if (amount > 4096) {
            uint32_t remaining = amount;
            while (remaining > 4096) {
                out.push_back(0x48); out.push_back(0x81); out.push_back(0xEC);
                out.push_back(0x00); out.push_back(0x10); out.push_back(0x00); out.push_back(0x00);
                out.push_back(0x48); out.push_back(0x85); out.push_back(0x04); out.push_back(0x24);
                remaining -= 4096;
            }
            if (remaining > 0) {
                if (remaining <= 127) {
                    out.push_back(0x48); out.push_back(0x83); out.push_back(0xEC);
                    out.push_back(static_cast<uint8_t>(remaining & 0xFF));
                } else {
                    out.push_back(0x48); out.push_back(0x81); out.push_back(0xEC);
                    out.push_back(static_cast<uint8_t>((remaining >> 0) & 0xFF));
                    out.push_back(static_cast<uint8_t>((remaining >> 8) & 0xFF));
                    out.push_back(static_cast<uint8_t>((remaining >> 16) & 0xFF));
                    out.push_back(static_cast<uint8_t>((remaining >> 24) & 0xFF));
                }
                out.push_back(0x48); out.push_back(0x85); out.push_back(0x04); out.push_back(0x24);
            }
            return;
        }
#endif
        if (amount <= 127) {
            out.push_back(0x48); out.push_back(0x83); out.push_back(0xEC);
            out.push_back(static_cast<uint8_t>(amount & 0xFF));
        } else {
            out.push_back(0x48); out.push_back(0x81); out.push_back(0xEC);
            out.push_back(static_cast<uint8_t>((amount >> 0) & 0xFF));
            out.push_back(static_cast<uint8_t>((amount >> 8) & 0xFF));
            out.push_back(static_cast<uint8_t>((amount >> 16) & 0xFF));
            out.push_back(static_cast<uint8_t>((amount >> 24) & 0xFF));
        }
    }

    static void emit_add_rsp(std::vector<uint8_t>& out, uint32_t amount) {
        if (amount == 0) return;
        if (amount <= 127) {
            out.push_back(0x48); out.push_back(0x83); out.push_back(0xC4);
            out.push_back(static_cast<uint8_t>(amount & 0xFF));
        } else {
            out.push_back(0x48); out.push_back(0x81); out.push_back(0xC4);
            out.push_back(static_cast<uint8_t>((amount >> 0) & 0xFF));
            out.push_back(static_cast<uint8_t>((amount >> 8) & 0xFF));
            out.push_back(static_cast<uint8_t>((amount >> 16) & 0xFF));
            out.push_back(static_cast<uint8_t>((amount >> 24) & 0xFF));
        }
    }

    static void emit_mov_reg_from_stack(std::vector<uint8_t>& out, uint8_t reg, int32_t offset) {
        out.push_back(exec::X64Encoding::rex(/*w=*/true,
                                             /*r=*/requires_rex(reg),
                                             /*x=*/false,
                                             /*b=*/false));
        out.push_back(0x8B); // MOV r64, [rsp + offset]
        if (offset >= -128 && offset <= 127) {
            out.push_back(static_cast<uint8_t>(0x44u | ((reg & 0x7u) << 3u)));
            out.push_back(0x24u);
            out.push_back(static_cast<uint8_t>(offset & 0xFF));
        } else {
            out.push_back(static_cast<uint8_t>(0x84u | ((reg & 0x7u) << 3u)));
            out.push_back(0x24u);
            out.push_back(static_cast<uint8_t>((offset >> 0) & 0xFF));
            out.push_back(static_cast<uint8_t>((offset >> 8) & 0xFF));
            out.push_back(static_cast<uint8_t>((offset >> 16) & 0xFF));
            out.push_back(static_cast<uint8_t>((offset >> 24) & 0xFF));
        }
    }

    static void emit_mov_stack_from_reg(std::vector<uint8_t>& out, int32_t offset, uint8_t reg) {
        out.push_back(exec::X64Encoding::rex(/*w=*/true,
                                             /*r=*/requires_rex(reg),
                                             /*x=*/false,
                                             /*b=*/false));
        out.push_back(0x89); // MOV [rsp + offset], r64
        if (offset >= -128 && offset <= 127) {
            out.push_back(static_cast<uint8_t>(0x44u | ((reg & 0x7u) << 3u)));
            out.push_back(0x24u);
            out.push_back(static_cast<uint8_t>(offset & 0xFF));
        } else {
            out.push_back(static_cast<uint8_t>(0x84u | ((reg & 0x7u) << 3u)));
            out.push_back(0x24u);
            out.push_back(static_cast<uint8_t>((offset >> 0) & 0xFF));
            out.push_back(static_cast<uint8_t>((offset >> 8) & 0xFF));
            out.push_back(static_cast<uint8_t>((offset >> 16) & 0xFF));
            out.push_back(static_cast<uint8_t>((offset >> 24) & 0xFF));
        }
    }

    static void emit_movsd_xmm_from_stack(std::vector<uint8_t>& out, uint8_t xmm_reg, int32_t offset) {
        out.push_back(0xF2);
        if (requires_rex(xmm_reg)) {
            out.push_back(exec::X64Encoding::rex(/*w=*/false,
                                                 /*r=*/true,
                                                 /*x=*/false,
                                                 /*b=*/false));
        }
        out.push_back(0x0F);
        out.push_back(0x10); // MOVSD xmm, [rsp + offset]
        if (offset >= -128 && offset <= 127) {
            out.push_back(static_cast<uint8_t>(0x44u | ((xmm_reg & 0x7u) << 3u)));
            out.push_back(0x24u);
            out.push_back(static_cast<uint8_t>(offset & 0xFF));
        } else {
            out.push_back(static_cast<uint8_t>(0x84u | ((xmm_reg & 0x7u) << 3u)));
            out.push_back(0x24u);
            out.push_back(static_cast<uint8_t>((offset >> 0) & 0xFF));
            out.push_back(static_cast<uint8_t>((offset >> 8) & 0xFF));
            out.push_back(static_cast<uint8_t>((offset >> 16) & 0xFF));
            out.push_back(static_cast<uint8_t>((offset >> 24) & 0xFF));
        }
    }

    static void emit_movsd_stack_from_xmm(std::vector<uint8_t>& out, int32_t offset, uint8_t xmm_reg) {
        out.push_back(0xF2);
        if (requires_rex(xmm_reg)) {
            out.push_back(exec::X64Encoding::rex(/*w=*/false,
                                                 /*r=*/true,
                                                 /*x=*/false,
                                                 /*b=*/false));
        }
        out.push_back(0x0F);
        out.push_back(0x11); // MOVSD [rsp + offset], xmm
        if (offset >= -128 && offset <= 127) {
            out.push_back(static_cast<uint8_t>(0x44u | ((xmm_reg & 0x7u) << 3u)));
            out.push_back(0x24u);
            out.push_back(static_cast<uint8_t>(offset & 0xFF));
        } else {
            out.push_back(static_cast<uint8_t>(0x84u | ((xmm_reg & 0x7u) << 3u)));
            out.push_back(0x24u);
            out.push_back(static_cast<uint8_t>((offset >> 0) & 0xFF));
            out.push_back(static_cast<uint8_t>((offset >> 8) & 0xFF));
            out.push_back(static_cast<uint8_t>((offset >> 16) & 0xFF));
            out.push_back(static_cast<uint8_t>((offset >> 24) & 0xFF));
        }
    }

    static void emit_movss_xmm_from_stack(std::vector<uint8_t>& out, uint8_t xmm_reg, int32_t offset) {
        out.push_back(0xF3);
        if (requires_rex(xmm_reg)) {
            out.push_back(exec::X64Encoding::rex(/*w=*/false,
                                                 /*r=*/true,
                                                 /*x=*/false,
                                                 /*b=*/false));
        }
        out.push_back(0x0F);
        out.push_back(0x10); // MOVSS xmm, [rsp + offset]
        if (offset >= -128 && offset <= 127) {
            out.push_back(static_cast<uint8_t>(0x44u | ((xmm_reg & 0x7u) << 3u)));
            out.push_back(0x24u);
            out.push_back(static_cast<uint8_t>(offset & 0xFF));
        } else {
            out.push_back(static_cast<uint8_t>(0x84u | ((xmm_reg & 0x7u) << 3u)));
            out.push_back(0x24u);
            out.push_back(static_cast<uint8_t>((offset >> 0) & 0xFF));
            out.push_back(static_cast<uint8_t>((offset >> 8) & 0xFF));
            out.push_back(static_cast<uint8_t>((offset >> 16) & 0xFF));
            out.push_back(static_cast<uint8_t>((offset >> 24) & 0xFF));
        }
    }

    static void emit_movss_stack_from_xmm(std::vector<uint8_t>& out, int32_t offset, uint8_t xmm_reg) {
        out.push_back(0xF3);
        if (requires_rex(xmm_reg)) {
            out.push_back(exec::X64Encoding::rex(/*w=*/false,
                                                 /*r=*/true,
                                                 /*x=*/false,
                                                 /*b=*/false));
        }
        out.push_back(0x0F);
        out.push_back(0x11); // MOVSS [rsp + offset], xmm
        if (offset >= -128 && offset <= 127) {
            out.push_back(static_cast<uint8_t>(0x44u | ((xmm_reg & 0x7u) << 3u)));
            out.push_back(0x24u);
            out.push_back(static_cast<uint8_t>(offset & 0xFF));
        } else {
            out.push_back(static_cast<uint8_t>(0x84u | ((xmm_reg & 0x7u) << 3u)));
            out.push_back(0x24u);
            out.push_back(static_cast<uint8_t>((offset >> 0) & 0xFF));
            out.push_back(static_cast<uint8_t>((offset >> 8) & 0xFF));
            out.push_back(static_cast<uint8_t>((offset >> 16) & 0xFF));
            out.push_back(static_cast<uint8_t>((offset >> 24) & 0xFF));
        }
    }

    static uint8_t map_reg(uint32_t vreg) noexcept {
        return static_cast<uint8_t>(exec::X64SinglePassRegisterMapper::map_gpr_branchless(vreg));
    }

    static constexpr bool requires_rex(uint8_t reg) noexcept {
        return reg >= 8u;
    }

    static void emit_movq_xmm_gpr(std::vector<uint8_t>& out, uint8_t xmm_dst, uint8_t gpr_src) {
        out.push_back(0x66);
        out.push_back(exec::X64Encoding::rex(/*w=*/true,
                                             /*r=*/requires_rex(xmm_dst),
                                             /*x=*/false,
                                             /*b=*/requires_rex(gpr_src)));
        out.push_back(0x0F);
        out.push_back(0x6E); // MOVQ xmm, r/m64
        emit_modrm_reg_reg(out, xmm_dst, gpr_src);
    }

    static void emit_movq_gpr_xmm(std::vector<uint8_t>& out, uint8_t gpr_dst, uint8_t xmm_src) {
        out.push_back(0x66);
        out.push_back(exec::X64Encoding::rex(/*w=*/true,
                                             /*r=*/requires_rex(xmm_src),
                                             /*x=*/false,
                                             /*b=*/requires_rex(gpr_dst)));
        out.push_back(0x0F);
        out.push_back(0x7E); // MOVQ r/m64, xmm
        emit_modrm_reg_reg(out, xmm_src, gpr_dst);
    }

    static uint8_t map_xmm_reg(uint32_t vreg) noexcept {
        return exec::X64SinglePassRegisterMapper::map_xmm_branchless(vreg);
    }

    static uint8_t map_zmm_reg(uint32_t vreg) noexcept {
        return exec::X64Encoding::map_DGTL_vec_to_phys(vreg);
    }

    static void emit_reg_move(std::vector<uint8_t>& out, uint8_t dst, uint8_t src) {
        out.push_back(exec::X64Encoding::rex(/*w=*/true,
                                             /*r=*/requires_rex(src),
                                             /*x=*/false,
                                             /*b=*/requires_rex(dst)));
        out.push_back(0x89); // MOV r/m64, r64
        emit_modrm_reg_reg(out, src, dst);
    }

    static void emit_reg_move32(std::vector<uint8_t>& out, uint8_t dst, uint8_t src) {
        out.push_back(exec::X64Encoding::rex(/*w=*/false,
                                             /*r=*/requires_rex(src),
                                             /*x=*/false,
                                             /*b=*/requires_rex(dst)));
        out.push_back(0x89); // MOV r/m32, r32
        emit_modrm_reg_reg(out, src, dst);
    }

    static void emit_xmm_move(std::vector<uint8_t>& out, uint8_t dst, uint8_t src) {
        out.push_back(0xF3); // MOVSS prefix
        if (requires_rex(dst) || requires_rex(src)) {
            out.push_back(exec::X64Encoding::rex(/*w=*/false,
                                                 /*r=*/requires_rex(dst),
                                                 /*x=*/false,
                                                 /*b=*/requires_rex(src)));
        }
        out.push_back(0x0F);
        out.push_back(0x10); // MOVSS xmm, xmm/m32
        emit_modrm_reg_reg(out, dst, src);
    }

    static void emit_xmm_move_f64(std::vector<uint8_t>& out, uint8_t dst, uint8_t src) {
        out.push_back(0xF2); // MOVSD prefix
        if (requires_rex(dst) || requires_rex(src)) {
            out.push_back(exec::X64Encoding::rex(/*w=*/false,
                                                 /*r=*/requires_rex(dst),
                                                 /*x=*/false,
                                                 /*b=*/requires_rex(src)));
        }
        out.push_back(0x0F);
        out.push_back(0x10); // MOVSD xmm, xmm/m64
        emit_modrm_reg_reg(out, dst, src);
    }

    /*
     * Kept as a compatibility helper for callers outside this migration unit.
     * It intentionally does not emit bytes: a partial EVEX prefix is never
     * acceptable machine code. Vector lowering must use the complete
     * MIR-native EVEX encoder.
     */
    static void emit_zmm_move(std::vector<uint8_t>& /*out*/,
                              uint32_t /*dst_vreg*/,
                              uint32_t /*src_vreg*/) = delete;

    static void emit_vex3_prefix(std::vector<uint8_t>& out,
                                 uint8_t dest,
                                 uint8_t src1,
                                 uint8_t src2,
                                 uint8_t pp,
                                 uint8_t mmmmm,
                                 bool w,
                                 bool l) {
        const uint8_t r_bit = requires_rex(dest) ? 0u : 1u;
        const uint8_t x_bit = 1u;
        const uint8_t b_bit = requires_rex(src2) ? 0u : 1u;
        const uint8_t vvvv = static_cast<uint8_t>(src1 & 0x0Fu);

        out.push_back(0xC4);
        out.push_back(static_cast<uint8_t>((r_bit << 7) | (x_bit << 6) | (b_bit << 5) | (mmmmm & 0x1Fu)));
        out.push_back(static_cast<uint8_t>((w ? 0x80u : 0u) | ((~vvvv & 0x0Fu) << 3) | (l ? 0x04u : 0u) | (pp & 0x03u)));
    }

    [[nodiscard]] uintptr_t resolve_runtime_function(std::string_view name) const noexcept {
        const auto it = runtime_functions_.find(std::string(name));
        if (it == runtime_functions_.end()) {
            return 0;
        }
        return static_cast<uintptr_t>(it->second);
    }

    [[nodiscard]] static const char* runtime_symbol_for_opcode(machine::MachineOpcode opcode) noexcept {
        return dgtl::mir::runtime_contract::runtime_symbol_for_opcode(opcode);
    }
    static bool is_virtual_reg(const machine::MachineRegister& reg) noexcept {
        return reg.valid() && reg.is_virtual();
    }

    static bool is_physical_reg(const machine::MachineRegister& reg) noexcept {
        return reg.valid() && reg.is_physical();
    }

    static std::uint32_t mir_reg_id(const machine::MachineOperand& operand) noexcept {
        return operand.kind == machine::MachineOperandKind::Register
            ? operand.reg.id
            : machine::INVALID_REG;
    }

    static std::int32_t mir_vreg_offset(const machine::MachineRegister& reg) noexcept {
        if (!is_virtual_reg(reg) || reg.id == 0u ||
            static_cast<std::uint64_t>(reg.id) > MAX_ADDRESSABLE_VREG) {
            return 40;
        }
        const std::uint64_t offset = 40ull + static_cast<std::uint64_t>(reg.id) * 8ull;
        if (offset > static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max())) {
            return 40;
        }
        return static_cast<std::int32_t>(offset);
    }

    static void mir_load_gpr(const machine::MachineOperand& operand,
                             std::uint8_t scratch,
                             std::vector<std::uint8_t>& out) {
        if (operand.kind != machine::MachineOperandKind::Register || !operand.reg.valid()) {
            return;
        }
        if (is_virtual_reg(operand.reg)) {
            emit_mov_reg_from_stack(out, scratch, mir_vreg_offset(operand.reg));
        } else {
            const std::uint8_t src = static_cast<std::uint8_t>(operand.reg.id & 0xFFu);
            if (src != scratch) {
                emit_reg_move(out, scratch, src);
            }
        }
    }

    static void mir_store_gpr(const machine::MachineOperand& operand,
                              std::uint8_t source,
                              std::vector<std::uint8_t>& out) {
        if (operand.kind != machine::MachineOperandKind::Register || !operand.reg.valid()) {
            return;
        }
        if (is_virtual_reg(operand.reg)) {
            emit_mov_stack_from_reg(out, mir_vreg_offset(operand.reg), source);
        } else {
            const std::uint8_t dst = static_cast<std::uint8_t>(operand.reg.id & 0xFFu);
            if (dst != source) {
                emit_reg_move(out, dst, source);
            }
        }
    }

    static void mir_load_f32(const machine::MachineOperand& operand,
                             std::uint8_t scratch,
                             std::vector<std::uint8_t>& out) {
        if (operand.kind != machine::MachineOperandKind::Register || !operand.reg.valid()) {
            return;
        }
        if (is_virtual_reg(operand.reg)) {
            emit_movss_xmm_from_stack(out, scratch, mir_vreg_offset(operand.reg));
        } else {
            const std::uint8_t src = static_cast<std::uint8_t>(operand.reg.id & 0xFFu);
            if (src != scratch) {
                emit_xmm_move(out, scratch, src);
            }
        }
    }

    static void mir_store_f32(const machine::MachineOperand& operand,
                              std::uint8_t source,
                              std::vector<std::uint8_t>& out) {
        if (operand.kind != machine::MachineOperandKind::Register || !operand.reg.valid()) {
            return;
        }
        if (is_virtual_reg(operand.reg)) {
            emit_movss_stack_from_xmm(out, mir_vreg_offset(operand.reg), source);
        } else {
            const std::uint8_t dst = static_cast<std::uint8_t>(operand.reg.id & 0xFFu);
            if (dst != source) {
                emit_xmm_move(out, dst, source);
            }
        }
    }

    static void mir_load_f64(const machine::MachineOperand& operand,
                             std::uint8_t scratch,
                             std::vector<std::uint8_t>& out) {
        if (operand.kind != machine::MachineOperandKind::Register || !operand.reg.valid()) {
            return;
        }
        if (is_virtual_reg(operand.reg)) {
            emit_movsd_xmm_from_stack(out, scratch, mir_vreg_offset(operand.reg));
        } else {
            const std::uint8_t src = static_cast<std::uint8_t>(operand.reg.id & 0xFFu);
            if (src != scratch) {
                emit_xmm_move_f64(out, scratch, src);
            }
        }
    }

    static void mir_store_f64(const machine::MachineOperand& operand,
                              std::uint8_t source,
                              std::vector<std::uint8_t>& out) {
        if (operand.kind != machine::MachineOperandKind::Register || !operand.reg.valid()) {
            return;
        }
        if (is_virtual_reg(operand.reg)) {
            emit_movsd_stack_from_xmm(out, mir_vreg_offset(operand.reg), source);
        } else {
            const std::uint8_t dst = static_cast<std::uint8_t>(operand.reg.id & 0xFFu);
            if (dst != source) {
                emit_xmm_move_f64(out, dst, source);
            }
        }
    }

    static const machine::MachineOperand* mir_def(const machine::MachineInstruction& instr) noexcept {
        for (const auto& operand : instr.operands) {
            if (operand.is_def()) return &operand;
        }
        return nullptr;
    }

    static const machine::MachineOperand* mir_use(const machine::MachineInstruction& instr,
                                                  std::size_t ordinal) noexcept {
        std::size_t seen = 0u;
        for (const auto& operand : instr.operands) {
            if (!operand.is_use()) continue;
            if (operand.kind == machine::MachineOperandKind::BasicBlock) continue;
            if (seen++ == ordinal) return &operand;
        }
        return nullptr;
    }

    static const machine::MachineOperand* mir_block_operand(const machine::MachineInstruction& instr,
                                                            std::size_t ordinal) noexcept {
        std::size_t seen = 0u;
        for (const auto& operand : instr.operands) {
            if (operand.kind != machine::MachineOperandKind::BasicBlock) continue;
            if (seen++ == ordinal) return &operand;
        }
        return nullptr;
    }

    static std::uint8_t x64_condition_code(machine::ComparisonPredicate pred) noexcept {
        switch (pred) {
            case machine::ComparisonPredicate::Equal:
                return 0x4; // E
            case machine::ComparisonPredicate::NotEqual:
                return 0x5; // NE
            case machine::ComparisonPredicate::SignedLessThan:
                return 0xC; // L
            case machine::ComparisonPredicate::SignedLessEqual:
                return 0xE; // LE
            case machine::ComparisonPredicate::SignedGreaterThan:
                return 0xF; // G
            case machine::ComparisonPredicate::SignedGreaterEqual:
                return 0xD; // GE
            case machine::ComparisonPredicate::UnsignedLessThan:
                return 0x2; // B
            case machine::ComparisonPredicate::UnsignedLessEqual:
                return 0x6; // BE
            case machine::ComparisonPredicate::UnsignedGreaterThan:
                return 0x7; // A
            case machine::ComparisonPredicate::UnsignedGreaterEqual:
                return 0x3; // AE
            case machine::ComparisonPredicate::Ordered:
                return 0xB; // NP
            case machine::ComparisonPredicate::Unordered:
                return 0xA; // P
            default:
                return 0x5;
        }
    }

    static std::uint8_t x64_condition_code(const machine::MachineInstruction& instr) noexcept {
        return x64_condition_code(instr.comparison_predicate());
    }

    static std::uint8_t x64_condition_code(machine::MachineOpcode opcode) noexcept {
        switch (opcode) {
            case machine::GM_ICMP32_EQ: case machine::GM_ICMP64_EQ:
            case machine::GM_FCMP32_EQ: case machine::GM_FCMP64_EQ:
            case machine::GM_BR_COMPARE_EQ:
                return 0x4;
            case machine::GM_ICMP32_NE: case machine::GM_ICMP64_NE:
            case machine::GM_FCMP32_NE: case machine::GM_FCMP64_NE:
            case machine::GM_BR_COMPARE_NE:
                return 0x5;
            case machine::GM_ICMP32_LT: case machine::GM_ICMP64_LT:
            case machine::GM_FCMP32_LT: case machine::GM_FCMP64_LT:
            case machine::GM_BR_COMPARE_LT:
                return 0xC;
            case machine::GM_ICMP32_LE: case machine::GM_ICMP64_LE:
            case machine::GM_FCMP32_LE: case machine::GM_FCMP64_LE:
            case machine::GM_BR_COMPARE_LE:
                return 0xE;
            case machine::GM_ICMP32_GT: case machine::GM_ICMP64_GT:
            case machine::GM_FCMP32_GT: case machine::GM_FCMP64_GT:
            case machine::GM_BR_COMPARE_GT:
                return 0xF;
            case machine::GM_ICMP32_GE: case machine::GM_ICMP64_GE:
            case machine::GM_FCMP32_GE: case machine::GM_FCMP64_GE:
            case machine::GM_BR_COMPARE_GE:
                return 0xD;
            case machine::GM_ICMP32:
            case machine::GM_ICMP64:
            case machine::GM_FCMP32:
            case machine::GM_FCMP64:
                return 0x5; // Default NE (not equal / true branch)
            default: return 0x5;
        }
    }

    static void emit_rel32_jump(std::vector<std::uint8_t>& out,
                                std::uint8_t condition,
                                std::vector<PendingBranchPatch>& patches,
                                machine::BlockId target) {
        if (condition == 0xFFu) {
            out.push_back(0xE9);
        } else {
            out.push_back(0x0F);
            out.push_back(static_cast<std::uint8_t>(0x80u | (condition & 0x0Fu)));
        }
        const std::size_t displacement_offset = out.size();
        out.insert(out.end(), 4u, 0u);
        patches.push_back(PendingBranchPatch{
            displacement_offset, out.size(), target
        });
    }

    static void emit_mir_i32_binary(machine::MachineSemanticKind semantic,
                                    const machine::MachineOperand& dst,
                                    const machine::MachineOperand& lhs,
                                    const machine::MachineOperand& rhs,
                                    std::vector<std::uint8_t>& out) {
        constexpr std::uint8_t r10 = static_cast<std::uint8_t>(exec::X64Reg::R10);
        constexpr std::uint8_t r11 = static_cast<std::uint8_t>(exec::X64Reg::R11);
        constexpr std::uint8_t rcx = static_cast<std::uint8_t>(exec::X64Reg::RCX);

        mir_load_gpr(lhs, r10, out);
        if (rhs.kind == machine::MachineOperandKind::Immediate) {
            emit_mov_imm32_signext(out, r11, static_cast<std::uint32_t>(rhs.immediate));
        } else {
            mir_load_gpr(rhs, r11, out);
        }

        switch (semantic) {
            case machine::MachineSemanticKind::IntegerAdd:
                out.push_back(exec::X64Encoding::rex(false, true, false, true)); out.push_back(0x01); emit_modrm_reg_reg(out, r11, r10); break;
            case machine::MachineSemanticKind::IntegerSub:
                out.push_back(exec::X64Encoding::rex(false, true, false, true)); out.push_back(0x29); emit_modrm_reg_reg(out, r11, r10); break;
            case machine::MachineSemanticKind::IntegerMul:
                out.push_back(exec::X64Encoding::rex(false, true, false, true));
                out.push_back(0x0F); out.push_back(0xAF); emit_modrm_reg_reg(out, r10, r11); break;
            case machine::MachineSemanticKind::IntegerAnd:
                out.push_back(exec::X64Encoding::rex(false, true, false, true)); out.push_back(0x21); emit_modrm_reg_reg(out, r11, r10); break;
            case machine::MachineSemanticKind::IntegerOr:
                out.push_back(exec::X64Encoding::rex(false, true, false, true)); out.push_back(0x09); emit_modrm_reg_reg(out, r11, r10); break;
            case machine::MachineSemanticKind::IntegerXor:
                out.push_back(exec::X64Encoding::rex(false, true, false, true)); out.push_back(0x31); emit_modrm_reg_reg(out, r11, r10); break;
            case machine::MachineSemanticKind::IntegerShl:
                emit_reg_move32(out, rcx, r11);
                out.push_back(0x41); out.push_back(0xD3); out.push_back(0xE2); break; // shl r10d,cl
            case machine::MachineSemanticKind::IntegerShr:
                emit_reg_move32(out, rcx, r11);
                out.push_back(0x41); out.push_back(0xD3); out.push_back(0xEA); break; // shr r10d,cl
            case machine::MachineSemanticKind::IntegerDiv:
            case machine::MachineSemanticKind::IntegerMod:
                emit_reg_move32(out, static_cast<std::uint8_t>(exec::X64Reg::RAX), r10);
                out.push_back(0x99); // CDQ
                out.push_back(exec::X64Encoding::rex(false, false, false, true));
                out.push_back(0xF7); emit_modrm_reg_reg(out, 7u, r11); // idiv r/m32
                if (semantic == machine::MachineSemanticKind::IntegerMod) {
                    emit_reg_move32(out, r10, static_cast<std::uint8_t>(exec::X64Reg::RDX));
                } else {
                    emit_reg_move32(out, r10, static_cast<std::uint8_t>(exec::X64Reg::RAX));
                }
                break;
            default:
                break;
        }
        mir_store_gpr(dst, r10, out);
    }

    static void emit_mir_i64_binary(machine::MachineSemanticKind semantic,
                                    const machine::MachineOperand& dst,
                                    const machine::MachineOperand& lhs,
                                    const machine::MachineOperand& rhs,
                                    std::vector<std::uint8_t>& out) {
        constexpr std::uint8_t r10 = static_cast<std::uint8_t>(exec::X64Reg::R10);
        constexpr std::uint8_t r11 = static_cast<std::uint8_t>(exec::X64Reg::R11);
        constexpr std::uint8_t rcx = static_cast<std::uint8_t>(exec::X64Reg::RCX);
        mir_load_gpr(lhs, r10, out);
        if (rhs.kind == machine::MachineOperandKind::Immediate) {
            emit_mov_imm32_signext(out, r11, static_cast<std::uint32_t>(rhs.immediate));
        } else {
            mir_load_gpr(rhs, r11, out);
        }
        switch (semantic) {
            case machine::MachineSemanticKind::IntegerAdd:
                out.push_back(exec::X64Encoding::rex(true, true, false, true));
                out.push_back(0x01); emit_modrm_reg_reg(out, r11, r10); break;
            case machine::MachineSemanticKind::IntegerSub:
                out.push_back(exec::X64Encoding::rex(true, true, false, true));
                out.push_back(0x29); emit_modrm_reg_reg(out, r11, r10); break;
            case machine::MachineSemanticKind::IntegerMul:
                out.push_back(exec::X64Encoding::rex(true, true, false, true));
                out.push_back(0x0F); out.push_back(0xAF); emit_modrm_reg_reg(out, r10, r11); break;
            case machine::MachineSemanticKind::IntegerAnd:
                out.push_back(exec::X64Encoding::rex(true, true, false, true));
                out.push_back(0x21); emit_modrm_reg_reg(out, r11, r10); break;
            case machine::MachineSemanticKind::IntegerOr:
                out.push_back(exec::X64Encoding::rex(true, true, false, true));
                out.push_back(0x09); emit_modrm_reg_reg(out, r11, r10); break;
            case machine::MachineSemanticKind::IntegerXor:
                out.push_back(exec::X64Encoding::rex(true, true, false, true));
                out.push_back(0x31); emit_modrm_reg_reg(out, r11, r10); break;
            case machine::MachineSemanticKind::IntegerShl:
                emit_reg_move(out, rcx, r11);
                out.push_back(0x49); out.push_back(0xD3); out.push_back(0xE2); break; // shl r10,cl
            case machine::MachineSemanticKind::IntegerShr:
                emit_reg_move(out, rcx, r11);
                out.push_back(0x49); out.push_back(0xD3); out.push_back(0xEA); break; // shr r10,cl
            case machine::MachineSemanticKind::IntegerDiv:
            case machine::MachineSemanticKind::IntegerMod:
                emit_reg_move(out, static_cast<std::uint8_t>(exec::X64Reg::RAX), r10);
                out.push_back(0x48); out.push_back(0x99); // CQO
                out.push_back(0x49); out.push_back(0xF7); emit_modrm_reg_reg(out, 7u, r11);
                if (semantic == machine::MachineSemanticKind::IntegerMod) {
                    emit_reg_move(out, r10, static_cast<std::uint8_t>(exec::X64Reg::RDX));
                } else {
                    emit_reg_move(out, r10, static_cast<std::uint8_t>(exec::X64Reg::RAX));
                }
                break;
            default:
                break;
        }
        mir_store_gpr(dst, r10, out);
    }

    static void emit_mir_float_binary(machine::MachineSemanticKind semantic,
                                      bool is_f64,
                                      const machine::MachineOperand& dst,
                                      const machine::MachineOperand& lhs,
                                      const machine::MachineOperand& rhs,
                                      std::vector<std::uint8_t>& out) {
        constexpr std::uint8_t xmm0 = 0u;
        constexpr std::uint8_t xmm1 = 1u;
        if (is_f64) {
            mir_load_f64(lhs, xmm0, out);
            mir_load_f64(rhs, xmm1, out);
        } else {
            mir_load_f32(rhs, xmm1, out);
        }
        const std::uint8_t prefix = is_f64 ? 0xF2u : 0xF3u;
        std::uint8_t opcode_byte = 0x58u;
        switch (semantic) {
            case machine::MachineSemanticKind::FloatAdd: opcode_byte = 0x58u; break;
            case machine::MachineSemanticKind::FloatSub: opcode_byte = 0x5Cu; break;
            case machine::MachineSemanticKind::FloatMul: opcode_byte = 0x59u; break;
            case machine::MachineSemanticKind::FloatDiv: opcode_byte = 0x5Eu; break;
            default: break;
        }
        out.push_back(prefix);
        out.push_back(0x0F); out.push_back(opcode_byte);
        emit_modrm_reg_reg(out, xmm0, xmm1);
        if (is_f64) mir_store_f64(dst, xmm0, out);
        else mir_store_f32(dst, xmm0, out);
    }

    bool emit_mir_native_instruction(
        const machine::MachineInstruction& instr,
        std::vector<std::uint8_t>& out,
        std::vector<PendingBranchPatch>& patches) const {
        const machine::MachineSemanticKind semantic = instr.semantic_kind;
        const machine::MachineOpcode opcode = instr.opcode;
        const machine::MachineOperand* def = mir_def(instr);

        switch (instr.form) {
            case machine::MachineForm::NoOp:
                return true;
            case machine::MachineForm::Move: {
                if (opcode == machine::GM_LOAD || opcode == machine::GM_LOAD_STACK) {
                    const auto* memory = mir_use(instr, 0u);
                    if (!def || !memory || memory->kind != machine::MachineOperandKind::Memory ||
                        !memory->memory.base.valid()) {
                        mark_error("MIR LOAD requires a memory operand", opcode);
                        return false;
                    }
                    constexpr std::uint8_t r11 = static_cast<std::uint8_t>(exec::X64Reg::R11);
                    constexpr std::uint8_t r10 = static_cast<std::uint8_t>(exec::X64Reg::R10);
                    const std::uint32_t width = memory->memory.size_bytes;
                    mir_load_gpr(machine::MachineOperand::use(memory->memory.base), r11, out);
                    if (def->reg.reg_class == machine::RegisterClassKind::FloatingPoint) {
                        if (width == 8u) {
                            out.push_back(0xF2); out.push_back(0x41); out.push_back(0x0F); out.push_back(0x10); out.push_back(0x03);
                            mir_store_f64(*def, 0u, out);
                        } else if (width == 4u) {
                            out.push_back(0xF3); out.push_back(0x41); out.push_back(0x0F); out.push_back(0x10); out.push_back(0x03);
                            mir_store_f32(*def, 0u, out);
                        } else {
                            mark_error("MIR floating LOAD width is unsupported", opcode);
                            return false;
                        }
                    } else if (width == 8u) {
                        out.push_back(0x4D); out.push_back(0x8B); out.push_back(0x13);
                        mir_store_gpr(*def, r10, out);
                    } else if (width == 4u) {
                        out.push_back(0x45); out.push_back(0x8B); out.push_back(0x13);
                        mir_store_gpr(*def, r10, out);
                    } else if (width == 2u) {
                        out.push_back(0x45); out.push_back(0x0F); out.push_back(0xB7); out.push_back(0x13);
                        mir_store_gpr(*def, r10, out);
                    } else if (width == 1u) {
                        out.push_back(0x45); out.push_back(0x0F); out.push_back(0xB6); out.push_back(0x13);
                        mir_store_gpr(*def, r10, out);
                    } else {
                        mark_error("MIR LOAD width is not supported by the X64 scalar encoder", opcode);
                        return false;
                    }
                    return true;
                }
                if (opcode == machine::GM_STORE || opcode == machine::GM_STORE_STACK) {
                    const auto* value = mir_use(instr, 0u);
                    const auto* memory = mir_use(instr, 1u);
                    if (!value || !memory || memory->kind != machine::MachineOperandKind::Memory ||
                        !memory->memory.base.valid()) {
                        mark_error("MIR STORE requires value and memory operands", opcode);
                        return false;
                    }
                    constexpr std::uint8_t r11 = static_cast<std::uint8_t>(exec::X64Reg::R11);
                    constexpr std::uint8_t r10 = static_cast<std::uint8_t>(exec::X64Reg::R10);
                    const std::uint32_t width = memory->memory.size_bytes;
                    mir_load_gpr(machine::MachineOperand::use(memory->memory.base), r11, out);
                    mir_load_gpr(*value, r10, out);
                    if (value->reg.reg_class == machine::RegisterClassKind::FloatingPoint) {
                        if (width == 8u) {
                            if (value->subregister == 32u) {
                            mir_load_f32(*value, 0u, out);
                        } else {
                            mir_load_f64(*value, 0u, out);
                        }
                            out.push_back(0xF2); out.push_back(0x41); out.push_back(0x0F); out.push_back(0x11); out.push_back(0x03);
                        } else if (width == 4u) {
                            mir_load_f32(*value, 0u, out);
                            out.push_back(0xF3); out.push_back(0x41); out.push_back(0x0F); out.push_back(0x11); out.push_back(0x03);
                        } else {
                            mark_error("MIR floating STORE width is unsupported", opcode);
                            return false;
                        }
                    } else if (width == 8u) {
                        out.push_back(0x4D); out.push_back(0x89); out.push_back(0x13);
                    } else if (width == 4u) {
                        out.push_back(0x45); out.push_back(0x89); out.push_back(0x13);
                    } else if (width == 2u) {
                        out.push_back(0x66); out.push_back(0x45); out.push_back(0x89); out.push_back(0x13);
                    } else if (width == 1u) {
                        out.push_back(0x45); out.push_back(0x88); out.push_back(0x13);
                    } else {
                        mark_error("MIR STORE width is not supported by the X64 scalar encoder", opcode);
                        return false;
                    }
                    return true;
                }
                const auto* src = mir_use(instr, 0u);
                if (!def || !src) {
                    mark_error("MIR Move requires destination and source", opcode);
                    return false;
                }
                constexpr std::uint8_t r10 = static_cast<std::uint8_t>(exec::X64Reg::R10);
                mir_load_gpr(*src, r10, out);
                mir_store_gpr(*def, r10, out);
                return true;
            }
            case machine::MachineForm::LoadImmediate: {
                if (!def) {
                    mark_error("MIR LoadImmediate requires a destination", opcode);
                    return false;
                }
                const auto* imm = mir_use(instr, 0u);
                constexpr std::uint8_t r10 = static_cast<std::uint8_t>(exec::X64Reg::R10);
                if (imm && imm->kind == machine::MachineOperandKind::Immediate) {
                    const std::uint64_t value = static_cast<std::uint64_t>(imm->immediate);
                    out.push_back(exec::X64Encoding::rex(true, false, false, true));
                    out.push_back(static_cast<std::uint8_t>(0xB8u + (r10 & 7u)));
                    append_u64(out, value);
                    mir_store_gpr(*def, r10, out);
                    return true;
                }
                if (imm && imm->kind == machine::MachineOperandKind::FloatingImmediate) {
                    const std::uint64_t bits = std::bit_cast<std::uint64_t>(imm->floating_immediate);
                    out.push_back(exec::X64Encoding::rex(true, false, false, true));
                    out.push_back(static_cast<std::uint8_t>(0xB8u + (r10 & 7u)));
                    append_u64(out, bits);
                    if (bits >> 32u != 0u) {
                        emit_movq_xmm_gpr(out, 0u, r10);
                        mir_store_f64(*def, 0u, out);
                    } else {
                        emit_movq_xmm_gpr(out, 0u, r10);
                        mir_store_f32(*def, 0u, out);
                    }
                    return true;
                }
                mark_error("MIR LoadImmediate has no encodable immediate operand", opcode);
                return false;
            }
            case machine::MachineForm::IntegerBinary32: {
                const auto* lhs = mir_use(instr, 0u); const auto* rhs = mir_use(instr, 1u);
                if (!def || !lhs || !rhs) { mark_error("MIR i32 binary operand contract violated", opcode); return false; }
                emit_mir_i32_binary(semantic, *def, *lhs, *rhs, out); return true;
            }
            case machine::MachineForm::IntegerBinary64: {
                const auto* lhs = mir_use(instr, 0u); const auto* rhs = mir_use(instr, 1u);
                if (!def || !lhs || !rhs) { mark_error("MIR i64 binary operand contract violated", opcode); return false; }
                emit_mir_i64_binary(semantic, *def, *lhs, *rhs, out); return true;
            }
            case machine::MachineForm::FloatBinary32: {
                const auto* lhs = mir_use(instr, 0u); const auto* rhs = mir_use(instr, 1u);
                if (!def || !lhs || !rhs) { mark_error("MIR f32 binary operand contract violated", opcode); return false; }
                emit_mir_float_binary(semantic, false, *def, *lhs, *rhs, out); return true;
            }
            case machine::MachineForm::FloatBinary64: {
                const auto* lhs = mir_use(instr, 0u); const auto* rhs = mir_use(instr, 1u);
                if (!def || !lhs || !rhs) { mark_error("MIR f64 binary operand contract violated", opcode); return false; }
                emit_mir_float_binary(semantic, true, *def, *lhs, *rhs, out); return true;
            }
            case machine::MachineForm::FloatTernaryFma32: {
                const auto* a = mir_use(instr, 0u); const auto* b = mir_use(instr, 1u); const auto* c = mir_use(instr, 2u);
                if (!def || !a || !b || !c) { mark_error("MIR FMA operand contract violated", opcode); return false; }
                // Semantically exact scalar fallback until the dedicated EVEX/FMA
                // target instruction contract carries operand ordering explicitly.
                mir_load_f32(*a, 0u, out); mir_load_f32(*b, 1u, out); mir_load_f32(*c, 2u, out);
                out.push_back(0xF3); out.push_back(0x0F); out.push_back(0x59); emit_modrm_reg_reg(out, 0u, 1u);
                out.push_back(0xF3); out.push_back(0x0F); out.push_back(0x58); emit_modrm_reg_reg(out, 0u, 2u);
                mir_store_f32(*def, 0u, out); return true;
            }
            case machine::MachineForm::Compare32: {
                const auto* lhs = mir_use(instr, 0u); const auto* rhs = mir_use(instr, 1u);
                if (!def || !lhs || !rhs) { mark_error("MIR compare operand contract violated", opcode); return false; }
                constexpr std::uint8_t r10 = static_cast<std::uint8_t>(exec::X64Reg::R10);
                constexpr std::uint8_t r11 = static_cast<std::uint8_t>(exec::X64Reg::R11);
                mir_load_gpr(*lhs, r10, out); mir_load_gpr(*rhs, r11, out);
                out.push_back(exec::X64Encoding::rex(false, true, false, true)); out.push_back(0x39); emit_modrm_reg_reg(out, r11, r10);
                out.push_back(exec::X64Encoding::rex(false, true, false, true)); out.push_back(0x0F); out.push_back(0x90u | x64_condition_code(instr)); emit_modrm_reg_reg(out, 0u, r10);
                // setcc writes only the low byte; zero-extend it before storing the i32 result.
                out.push_back(exec::X64Encoding::rex(false, false, false, true)); out.push_back(0x0F); out.push_back(0xB6); emit_modrm_reg_reg(out, r10, r10);
                mir_store_gpr(*def, r10, out); return true;
            }
            case machine::MachineForm::Goto: {
                const auto* target = mir_block_operand(instr, 0u);
                if (!target) { mark_error("MIR GOTO has no basic-block operand", opcode); return false; }
                emit_rel32_jump(out, 0xFFu, patches, target->block); return true;
            }
            case machine::MachineForm::BranchCond: {
                const auto* cond = mir_use(instr, 0u);
                const auto* true_target = mir_block_operand(instr, 0u);
                const auto* false_target = mir_block_operand(instr, 1u);
                if (!cond || !true_target || !false_target) { mark_error("MIR BranchCond operand contract violated", opcode); return false; }
                constexpr std::uint8_t r10 = static_cast<std::uint8_t>(exec::X64Reg::R10);
                mir_load_gpr(*cond, r10, out);
                out.push_back(exec::X64Encoding::rex(true, true, false, true));
                out.push_back(0x85); emit_modrm_reg_reg(out, r10, r10);
                emit_rel32_jump(out, 0x5u, patches, true_target->block); // JNE true
                emit_rel32_jump(out, 0xFFu, patches, false_target->block); // JMP false
                return true;
            }
            case machine::MachineForm::BranchCompare: {
                const auto* lhs = mir_use(instr, 0u); const auto* rhs = mir_use(instr, 1u); const auto* target = mir_block_operand(instr, 0u);
                if (!lhs || !rhs || !target) { mark_error("MIR BranchCompare operand contract violated", opcode); return false; }
                constexpr std::uint8_t r10 = static_cast<std::uint8_t>(exec::X64Reg::R10);
                constexpr std::uint8_t r11 = static_cast<std::uint8_t>(exec::X64Reg::R11);
                mir_load_gpr(*lhs, r10, out); mir_load_gpr(*rhs, r11, out);
                out.push_back(exec::X64Encoding::rex(false, true, false, true)); out.push_back(0x39); emit_modrm_reg_reg(out, r11, r10);
                const std::uint8_t cc = x64_condition_code(instr);
                emit_rel32_jump(out, cc, patches, target->block);
                const auto* false_target = mir_block_operand(instr, 1u);
                if (false_target) {
                    emit_rel32_jump(out, 0xFFu, patches, false_target->block);
                }
                return true;
            }
            case machine::MachineForm::Return: {
                const auto* value = mir_use(instr, 0u);
                if (value) {
                    if (value->reg.reg_class == machine::RegisterClassKind::FloatingPoint) {
                        // The current MIR return contract carries register class,
                        // so floating returns can be emitted through XMM0 without
                        // consulting the source IR type.
                        mir_load_f64(*value, 0u, out);
                    } else {
                        constexpr std::uint8_t rax = static_cast<std::uint8_t>(exec::X64Reg::RAX);
                        mir_load_gpr(*value, rax, out);
                    }
                } else {
                    out.push_back(0x31); out.push_back(0xC0); // xor eax,eax
                }
                emit_add_rsp(out, current_frame_size_);
                out.push_back(0xC3);
                emitted_return_ = true;
                return true;
            }
            case machine::MachineForm::VectorBinary512:
            case machine::MachineForm::VectorGather512:
            case machine::MachineForm::VectorStore512:
                mark_error("MIR vector form requires dedicated EVEX operand encoding", opcode);
                return false;
            case machine::MachineForm::Call: {
                const char* symbol = dgtl::mir::runtime_contract::runtime_symbol_for_semantic_kind(semantic);
                uintptr_t target_addr = 0;
                if (symbol != nullptr) {
                    target_addr = resolve_runtime_function(symbol);
                }
                const auto* tgt_op = mir_use(instr, 0u);
                if (tgt_op && tgt_op->is_immediate()) {
                    target_addr = static_cast<uintptr_t>(tgt_op->immediate);
                }

                constexpr std::uint8_t arg_gprs[4] = {
                    static_cast<std::uint8_t>(exec::X64Reg::RCX),
                    static_cast<std::uint8_t>(exec::X64Reg::RDX),
                    static_cast<std::uint8_t>(exec::X64Reg::R8),
                    static_cast<std::uint8_t>(exec::X64Reg::R9)
                };
                const std::size_t start_arg = (tgt_op && (tgt_op->is_immediate() || tgt_op->is_register())) ? 1u : 0u;
                std::size_t gpr_idx = 0;
                for (std::size_t ai = start_arg; ai < instr.operands.size() && gpr_idx < 4; ++ai) {
                    const auto& op = instr.operands[ai];
                    if (op.is_register()) {
                        if (op.reg.reg_class == machine::RegisterClassKind::FloatingPoint) {
                            mir_load_f64(op, static_cast<std::uint8_t>(gpr_idx), out);
                        } else {
                            mir_load_gpr(op, arg_gprs[gpr_idx], out);
                        }
                        ++gpr_idx;
                    }
                }

                if (target_addr != 0) {
                    constexpr std::uint8_t rax = static_cast<std::uint8_t>(exec::X64Reg::RAX);
                    out.push_back(exec::X64Encoding::rex(true, false, false, false));
                    out.push_back(static_cast<std::uint8_t>(0xB8u + rax));
                    append_u64(out, static_cast<uint64_t>(target_addr));
                    out.push_back(0xFF);
                    out.push_back(0xD0);
                } else if (tgt_op && tgt_op->is_register()) {
                    constexpr std::uint8_t r11 = static_cast<std::uint8_t>(exec::X64Reg::R11);
                    mir_load_gpr(*tgt_op, r11, out);
                    out.push_back(exec::X64Encoding::rex(false, false, false, true));
                    out.push_back(0xFF);
                    out.push_back(static_cast<std::uint8_t>(0xD0u | (r11 & 7u)));
                } else {
                    mark_error("MIR Call target cannot be resolved", opcode);
                    return false;
                }

                if (def && def->is_register()) {
                    if (def->reg.reg_class == machine::RegisterClassKind::FloatingPoint) {
                        mir_store_f64(*def, 0u, out);
                    } else {
                        constexpr std::uint8_t rax = static_cast<std::uint8_t>(exec::X64Reg::RAX);
                        mir_store_gpr(*def, rax, out);
                    }
                }
                return true;
            }
            case machine::MachineForm::Syscall: {
                const auto* sys_nr = mir_use(instr, 0u);
                if (sys_nr) {
                    constexpr std::uint8_t rax = static_cast<std::uint8_t>(exec::X64Reg::RAX);
                    if (sys_nr->is_immediate()) {
                        out.push_back(exec::X64Encoding::rex(true, false, false, false));
                        out.push_back(static_cast<std::uint8_t>(0xB8u + rax));
                        append_u64(out, static_cast<uint64_t>(sys_nr->immediate));
                    } else {
                        mir_load_gpr(*sys_nr, rax, out);
                    }
                }
                out.push_back(0x0F);
                out.push_back(0x05);
                if (def && def->is_register()) {
                    constexpr std::uint8_t rax = static_cast<std::uint8_t>(exec::X64Reg::RAX);
                    mir_store_gpr(*def, rax, out);
                }
                return true;
            }
            case machine::MachineForm::HardwareFence: {
                out.push_back(0x0F);
                out.push_back(0xAE);
                if (opcode == machine::GM_ACQUIRE_FENCE) {
                    out.push_back(0xE8);
                } else if (opcode == machine::GM_RELEASE_FENCE) {
                    out.push_back(0xF8);
                } else {
                    out.push_back(0xF0);
                }
                return true;
            }
            case machine::MachineForm::ConcurrentSync: {
                out.push_back(0x0F);
                out.push_back(0xAE);
                out.push_back(0xF0);
                return true;
            }
            case machine::MachineForm::AtomicOp: {
                const auto* ptr = mir_use(instr, 0u);
                const auto* val = mir_use(instr, 1u);
                if (!ptr || !val) { mark_error("MIR AtomicOp operand contract violated", opcode); return false; }
                constexpr std::uint8_t r10 = static_cast<std::uint8_t>(exec::X64Reg::R10);
                constexpr std::uint8_t r11 = static_cast<std::uint8_t>(exec::X64Reg::R11);
                mir_load_gpr(*ptr, r11, out);
                mir_load_gpr(*val, r10, out);
                out.push_back(0xF0);
                out.push_back(exec::X64Encoding::rex(true, true, false, true));
                out.push_back(0x0F);
                out.push_back(0xC1);
                emit_modrm_reg_reg(out, r10, r11);
                if (def && def->is_register()) {
                    mir_store_gpr(*def, r10, out);
                }
                return true;
            }
            case machine::MachineForm::LoadStack: {
                if (!def) { mark_error("MIR LoadStack has no def operand", opcode); return false; }
                const auto* slot = mir_use(instr, 0u);
                const std::int32_t offset = slot && slot->is_immediate() ? static_cast<std::int32_t>(slot->immediate) : 0;
                constexpr std::uint8_t r10 = static_cast<std::uint8_t>(exec::X64Reg::R10);
                emit_mov_reg_from_stack(out, r10, offset);
                mir_store_gpr(*def, r10, out);
                return true;
            }
            case machine::MachineForm::StoreStack: {
                const auto* val = mir_use(instr, 0u);
                const auto* slot = mir_use(instr, 1u);
                if (!val) { mark_error("MIR StoreStack has no value operand", opcode); return false; }
                const std::int32_t offset = slot && slot->is_immediate() ? static_cast<std::int32_t>(slot->immediate) : 0;
                constexpr std::uint8_t r10 = static_cast<std::uint8_t>(exec::X64Reg::R10);
                mir_load_gpr(*val, r10, out);
                emit_mov_stack_from_reg(out, offset, r10);
                return true;
            }
            case machine::MachineForm::TensorOp:
            case machine::MachineForm::Unhandled:
            default:
                mark_error("Unsupported MIR form at X64 byte-emission boundary", opcode);
                return false;
        }
    }

    bool emit_mir_native_function(
        const machine::MachineFunction& function,
        std::uint32_t frame_size,
        std::vector<std::uint8_t>& out,
        std::unordered_map<machine::BlockId, std::size_t>& block_offsets,
        std::vector<PendingBranchPatch>& patches) const {
        current_frame_size_ = frame_size;
        emitted_return_ = false;
        block_offsets.clear();

        const auto blocks = function.blocks();
        if (blocks.empty()) {
            mark_error("MIR function has no basic blocks", machine::GM_NOOP);
            return false;
        }

        if (!function.is_phase_at_least(machine::MachinePhase::FrameLowered) ||
            function.has_virtual_registers() ||
            !function.properties().registers_allocated ||
            !function.properties().frame_lowered) {
            mark_error(
                "X64 MIR encoder requires physical registers and a lowered MachineFrameInfo",
                machine::GM_NOOP);
            return false;
        }

        // The frame size now comes from MachineFrameInfo.  The encoder does not
        // derive stack storage from IR result ids.
        emit_sub_rsp(out, frame_size);

        for (const auto& block : blocks) {
            block_offsets[block.id] = out.size();
            for (const machine::InstrId instruction_id : block.instructions) {
                const machine::MachineInstruction* instruction = function.instruction(instruction_id);
                if (instruction == nullptr) {
                    mark_error("MIR block references an invalid instruction", machine::GM_NOOP);
                    return false;
                }
                if (!emit_mir_native_instruction(*instruction, out, patches)) {
                    return false;
                }
            }
        }

        if (!emitted_return_) {
            emit_add_rsp(out, frame_size);
            out.push_back(0xC3);
        }
        return !had_error_;
    }

    static uint64_t combine_u64_from_ops(const machine::MachineOperand& op_a, const machine::MachineOperand& op_b) noexcept {
        return (static_cast<uint64_t>(op_b.immediate) << 32u) | static_cast<uint64_t>(op_a.immediate);
    }

    static uint64_t immediate_from_instruction(const machine::MachineInstruction& inst) noexcept {
        for (const auto& op : inst.operands) {
            if (op.kind == machine::MachineOperandKind::Immediate) {
                return static_cast<uint64_t>(op.immediate);
            }
        }
        return 0ull;
    }

    static void emit_load_imm(const machine::MachineInstruction& inst, std::vector<uint8_t>& out) {
        const auto* def = mir_def(inst);
        if (!def) return;
        const uint64_t imm = immediate_from_instruction(inst);
        if (def->kind == machine::MachineOperandKind::Register && def->reg.valid()) {
            constexpr uint8_t r10 = static_cast<uint8_t>(exec::X64Reg::R10);
            out.push_back(exec::X64Encoding::rex(true, false, false, (r10 >= 8u)));
            out.push_back(static_cast<uint8_t>(0xB8u + (r10 & 0x7u)));
            append_u64(out, imm);
            mir_store_gpr(*def, r10, out);
        }
    }

    static void emit_load_imm_f64(const machine::MachineInstruction& inst, std::vector<uint8_t>& out) {
        const auto* def = mir_def(inst);
        if (!def) return;
        const uint64_t imm = immediate_from_instruction(inst);
        const uint8_t rax = static_cast<uint8_t>(exec::X64Reg::RAX);
        out.push_back(exec::X64Encoding::rex(true, false, false, false));
        out.push_back(static_cast<uint8_t>(0xB8u + (rax & 0x7u)));
        append_u64(out, imm);
        out.push_back(0x66);
        out.push_back(exec::X64Encoding::rex(true, false, false, false));
        out.push_back(0x0F);
        out.push_back(0x6E);
        emit_modrm_reg_reg(out, 0u, rax);
        mir_store_f64(*def, 0u, out);
    }

    void emit_function_prologue_params(const machine::MachineFunction& function,
                                       std::vector<uint8_t>& out,
                                       uint32_t frame_size) const {
        (void)function; (void)out; (void)frame_size;
    }

    static void emit_move(const machine::MachineInstruction& inst, std::vector<uint8_t>& out) {
        const auto* def = mir_def(inst);
        const auto* src = mir_use(inst, 0u);
        if (!def || !src) return;
        constexpr uint8_t r10 = static_cast<uint8_t>(exec::X64Reg::R10);
        mir_load_gpr(*src, r10, out);
        mir_store_gpr(*def, r10, out);
    }

    static void emit_memory_store(const machine::MachineInstruction& inst, std::vector<uint8_t>& out) {
        const auto* val = mir_use(inst, 0u);
        const auto* ptr = mir_use(inst, 1u);
        if (!val || !ptr) return;
        constexpr uint8_t r10 = static_cast<uint8_t>(exec::X64Reg::R10);
        constexpr uint8_t r11 = static_cast<uint8_t>(exec::X64Reg::R11);
        mir_load_gpr(*val, r10, out);
        mir_load_gpr(*ptr, r11, out);
        out.push_back(0x4D); out.push_back(0x89); out.push_back(0x13);
    }

    static void emit_memory_load(const machine::MachineInstruction& inst, std::vector<uint8_t>& out) {
        const auto* def = mir_def(inst);
        const auto* ptr = mir_use(inst, 0u);
        if (!def || !ptr) return;
        constexpr uint8_t r10 = static_cast<uint8_t>(exec::X64Reg::R10);
        constexpr uint8_t r11 = static_cast<uint8_t>(exec::X64Reg::R11);
        mir_load_gpr(*ptr, r11, out);
        out.push_back(0x4D); out.push_back(0x8B); out.push_back(0x13);
        mir_store_gpr(*def, r10, out);
    }

    static void emit_load_stack(const machine::MachineInstruction& inst, std::vector<uint8_t>& out) {
        const auto* def = mir_def(inst);
        const auto* src = mir_use(inst, 0u);
        if (!def || !src) return;
        constexpr uint8_t r10 = static_cast<uint8_t>(exec::X64Reg::R10);
        mir_load_gpr(*src, r10, out);
        mir_store_gpr(*def, r10, out);
    }

    static void emit_store_stack(const machine::MachineInstruction& inst, std::vector<uint8_t>& out) {
        const auto* def = mir_def(inst);
        const auto* src = mir_use(inst, 0u);
        if (!def || !src) return;
        constexpr uint8_t r10 = static_cast<uint8_t>(exec::X64Reg::R10);
        mir_load_gpr(*src, r10, out);
        mir_store_gpr(*def, r10, out);
    }

    static void emit_select(const machine::MachineInstruction& inst, std::vector<uint8_t>& out) {
        const auto* def = mir_def(inst);
        const auto* cond = mir_use(inst, 0u);
        const auto* true_val = mir_use(inst, 1u);
        const auto* false_val = mir_use(inst, 2u);
        if (!def || !cond || !true_val || !false_val) return;
        constexpr uint8_t r10 = static_cast<uint8_t>(exec::X64Reg::R10);
        constexpr uint8_t r11 = static_cast<uint8_t>(exec::X64Reg::R11);
        constexpr uint8_t rax = static_cast<uint8_t>(exec::X64Reg::RAX);
        mir_load_gpr(*cond, rax, out);
        mir_load_gpr(*true_val, r10, out);
        mir_load_gpr(*false_val, r11, out);
        out.push_back(0x48); out.push_back(0x85); out.push_back(0xC0);
        out.push_back(0x4D); out.push_back(0x0F); out.push_back(0x45); out.push_back(0xD3);
        mir_store_gpr(*def, r10, out);
    }

    static void emit_gep(const machine::MachineInstruction& inst, std::vector<uint8_t>& out) {
        const auto* def = mir_def(inst);
        const auto* base = mir_use(inst, 0u);
        const auto* offset = mir_use(inst, 1u);
        if (!def || !base) return;
        constexpr uint8_t r10 = static_cast<uint8_t>(exec::X64Reg::R10);
        constexpr uint8_t r11 = static_cast<uint8_t>(exec::X64Reg::R11);
        mir_load_gpr(*base, r10, out);
        if (offset) {
            mir_load_gpr(*offset, r11, out);
            out.push_back(exec::X64Encoding::rex(true, true, false, true));
            out.push_back(0x01); emit_modrm_reg_reg(out, r11, r10);
        }
        mir_store_gpr(*def, r10, out);
    }

    static void emit_i32_binary(const machine::MachineInstruction& inst, std::vector<uint8_t>& out) {
        const auto* def = mir_def(inst);
        const auto* lhs = mir_use(inst, 0u);
        const auto* rhs = mir_use(inst, 1u);
        if (def && lhs && rhs) {
            emit_mir_i32_binary(inst.semantic_kind, *def, *lhs, *rhs, out);
        }
    }

    static void emit_i64_binary(const machine::MachineInstruction& inst, std::vector<uint8_t>& out) {
        const auto* def = mir_def(inst);
        const auto* lhs = mir_use(inst, 0u);
        const auto* rhs = mir_use(inst, 1u);
        if (def && lhs && rhs) {
            emit_mir_i64_binary(inst.semantic_kind, *def, *lhs, *rhs, out);
        }
    }

    static void emit_f32_binary(const machine::MachineInstruction& inst, std::vector<uint8_t>& out) {
        const auto* def = mir_def(inst);
        const auto* lhs = mir_use(inst, 0u);
        const auto* rhs = mir_use(inst, 1u);
        if (def && lhs && rhs) {
            emit_mir_float_binary(inst.semantic_kind, false, *def, *lhs, *rhs, out);
        }
    }

    static void emit_f64_binary(const machine::MachineInstruction& inst, std::vector<uint8_t>& out) {
        const auto* def = mir_def(inst);
        const auto* lhs = mir_use(inst, 0u);
        const auto* rhs = mir_use(inst, 1u);
        if (def && lhs && rhs) {
            emit_mir_float_binary(inst.semantic_kind, true, *def, *lhs, *rhs, out);
        }
    }

    static void emit_f32_fma(const machine::MachineInstruction& inst, std::vector<uint8_t>& out) {
        const auto* def = mir_def(inst);
        const auto* a = mir_use(inst, 0u);
        const auto* b = mir_use(inst, 1u);
        const auto* c = mir_use(inst, 2u);
        if (!def || !a || !b || !c) return;
        mir_load_f32(*a, 0u, out);
        mir_load_f32(*b, 1u, out);
        mir_load_f32(*c, 2u, out);
        out.push_back(0xC4); out.push_back(0xE2); out.push_back(0x71); out.push_back(0xA9);
        emit_modrm_reg_reg(out, 0u, 2u);
        mir_store_f32(*def, 0u, out);
    }

    static void emit_cmp_i32(const machine::MachineInstruction& inst, std::vector<uint8_t>& out) {
        const auto* lhs = mir_use(inst, 0u);
        const auto* rhs = mir_use(inst, 1u);
        if (!lhs || !rhs) return;
        constexpr uint8_t r10 = static_cast<uint8_t>(exec::X64Reg::R10);
        constexpr uint8_t r11 = static_cast<uint8_t>(exec::X64Reg::R11);
        mir_load_gpr(*lhs, r10, out);
        mir_load_gpr(*rhs, r11, out);
        out.push_back(exec::X64Encoding::rex(false, true, false, true));
        out.push_back(0x39); emit_modrm_reg_reg(out, r11, r10);
    }

    void emit_vec_binary512(const machine::MachineInstruction& inst, std::vector<uint8_t>& out) {
        (void)inst; (void)out;
    }

    void emit_vec_gather512(const machine::MachineInstruction& inst, std::vector<uint8_t>& out) {
        (void)inst; (void)out;
    }

    void emit_vec_store512(const machine::MachineInstruction& inst, std::vector<uint8_t>& out) {
        (void)inst; (void)out;
    }

    static void emit_goto(const machine::MachineInstruction& inst, std::vector<uint8_t>& out, std::vector<PendingBranchPatch>& patches) {
        const auto* target = mir_block_operand(inst, 0u);
        if (target) {
            emit_rel32_jump(out, 0xFFu, patches, target->block);
        }
    }

    static void emit_branch_cond(const machine::MachineInstruction& inst, std::vector<uint8_t>& out, std::vector<PendingBranchPatch>& patches) {
        const auto* cond = mir_use(inst, 0u);
        const auto* target = mir_block_operand(inst, 1u);
        if (cond && target) {
            constexpr uint8_t rax = static_cast<uint8_t>(exec::X64Reg::RAX);
            mir_load_gpr(*cond, rax, out);
            out.push_back(0x48); out.push_back(0x85); out.push_back(0xC0);
            emit_rel32_jump(out, 0x05u, patches, target->block);
        }
    }

    static void emit_branch_compare_fused(const machine::MachineInstruction& inst, std::vector<uint8_t>& out, std::vector<PendingBranchPatch>& patches) {
        const auto* lhs = mir_use(inst, 0u);
        const auto* rhs = mir_use(inst, 1u);
        const auto* target = mir_block_operand(inst, 2u);
        if (lhs && rhs && target) {
            constexpr uint8_t r10 = static_cast<uint8_t>(exec::X64Reg::R10);
            constexpr uint8_t r11 = static_cast<uint8_t>(exec::X64Reg::R11);
            mir_load_gpr(*lhs, r10, out);
            mir_load_gpr(*rhs, r11, out);
            out.push_back(exec::X64Encoding::rex(true, true, false, true));
            out.push_back(0x39); emit_modrm_reg_reg(out, r11, r10);
            const uint8_t cc = x64_condition_code(inst);
            emit_rel32_jump(out, cc, patches, target->block);
        }
    }

    static void emit_syscall(const machine::MachineInstruction&, std::vector<uint8_t>& out) {
        out.push_back(0x0F);
        out.push_back(0x05);
    }

    void emit_call_address(uintptr_t address, const machine::MachineInstruction& inst, std::vector<uint8_t>& out) const {
        (void)inst;
        const uint8_t rax = static_cast<uint8_t>(exec::X64Reg::RAX);
        out.push_back(exec::X64Encoding::rex(true, false, false, false));
        out.push_back(static_cast<uint8_t>(0xB8u + (rax & 0x7u)));
        append_u64(out, static_cast<uint64_t>(address));
        out.push_back(0xFF);
        out.push_back(static_cast<uint8_t>(0xD0u | (rax & 0x7u)));
    }

    static bool emit_call_fastpath(const machine::MachineInstruction& inst, std::vector<uint8_t>& out) {
        (void)inst; (void)out;
        return false;
    }

    void emit_call_slot(const machine::MachineInstruction& inst, std::vector<uint8_t>& out) {
        (void)inst; (void)out;
    }

    void mark_error(const char* msg, machine::MachineOpcode opcode = machine::GM_NOOP) const {
        if (!had_error_) [[unlikely]] {
            had_error_ = true;
            last_error_ = msg;
            last_failed_opcode_ = opcode;
        }
    }

    template <typename T>
    requires (!std::is_same_v<T, machine::MachineOpcode>)
    void mark_error(const char* msg, T) const {
        mark_error(msg, machine::GM_NOOP);
    }

private:
    core::TargetMachine target_machine_;
    std::unordered_map<std::string, uintptr_t> runtime_functions_;
    std::unordered_map<uint32_t, uintptr_t> runtime_slots_;
    mutable std::unordered_map<uint32_t, size_t> label_offsets_;
    mutable bool had_error_ = false;
    mutable std::string last_error_;
    mutable machine::MachineOpcode last_failed_opcode_ = machine::GM_NOOP;
    mutable std::uint32_t current_frame_size_ = 0u;
    mutable bool emitted_return_ = false;
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
