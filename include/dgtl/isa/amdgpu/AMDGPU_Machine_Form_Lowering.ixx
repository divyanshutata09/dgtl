/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Component:   ROCm / AMDGPU Machine Form Lowering & MIR Construction
 * Standards:   C++26 / Clang 18+ / MSVC 19.40+ / GCC 14+
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

module;

#include <cstdlib>
#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

export module dgtl.isa.amdgpu.rocm_machine_form_lowering;

import dgtl.mir.instruction_selector;
import dgtl.mir.runtime_contract;
import dgtl.mir.legalizer;
import dgtl.mir.isel_gen;
import dgtl.mir.machine_ir;
import dgtl.isa.amdgpu.isel_gen;
import dgtl.isa.amdgpu.rocm_instruction_info;
import dgtl.isa.amdgpu.frame_lowering;
import dgtl.isa.amdgpu.rocm_reg_map;
import dgtl.mir.regalloc.universal;
import dgtl.target;

export namespace dgtl::isa::lowering {

using SelectedInstruction = dgtl::mir::isel::SelectedInstruction;
using InstructionSelectorProgram = dgtl::mir::isel::InstructionSelectorProgram;

/**
 * @brief ROCmFormLowering
 *
 * Dispatches SelectedInstruction forms and semantic kinds to ROCm AMDGPU GCN/CDNA emitter routines.
 */
class ROCmFormLowering {
public:
    using ByteStream = std::vector<uint8_t>;
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

    explicit ROCmFormLowering(const EncoderCallbacks& cbs) : callbacks_(cbs) {}

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
                if (callbacks_.fail_emission) callbacks_.fail_emission("Unhandled ROCm IntegerBinary32 opcode", static_cast<uint32_t>(inst.opcode)); return;
        }
    }

    inline void lower_integer_binary32(const lowering::SelectedInstruction& selected, ByteStream& out) {
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
                if (callbacks_.and_i64) callbacks_.and_i64(inst, out); return;
            case machine::MachineSemanticKind::IntegerOr:
                if (callbacks_.or_i64) callbacks_.or_i64(inst, out); return;
            case machine::MachineSemanticKind::IntegerXor:
                if (callbacks_.xor_i64) callbacks_.xor_i64(inst, out); return;
            case machine::MachineSemanticKind::IntegerShl:
                if (callbacks_.shl_i64) callbacks_.shl_i64(inst, out); return;
            case machine::MachineSemanticKind::IntegerShr:
                if (callbacks_.shr_i64) callbacks_.shr_i64(inst, out); return;
            default:
                if (callbacks_.fail_emission) callbacks_.fail_emission("Unhandled ROCm IntegerBinary64 opcode", static_cast<uint32_t>(inst.opcode)); return;
        }
    }

    inline void lower_integer_binary64(const lowering::SelectedInstruction& selected, ByteStream& out) {
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
                if (callbacks_.sub_f32) callbacks_.sub_f32(inst, out); return;
            case machine::MachineSemanticKind::FloatMul:
                if (callbacks_.mul_f32) callbacks_.mul_f32(inst, out); return;
            case machine::MachineSemanticKind::FloatDiv:
                if (callbacks_.div_f32) callbacks_.div_f32(inst, out); return;
            default:
                if (callbacks_.fail_emission) callbacks_.fail_emission("Unhandled ROCm FloatBinary32 opcode", static_cast<uint32_t>(inst.opcode)); return;
        }
    }

    inline void lower_float_binary32(const lowering::SelectedInstruction& selected, ByteStream& out) {
        machine::MachineInstruction inst;
        inst.form = selected.form;
        inst.semantic_kind = selected.semantic_kind;
        lower_float_binary32(inst, out);
    }

    inline void lower_float_binary64(const machine::MachineInstruction& inst, ByteStream& out) {
        switch (inst.semantic_kind) {
            case machine::MachineSemanticKind::FloatAdd:
                if (callbacks_.add_f64) callbacks_.add_f64(inst, out); return;
            case machine::MachineSemanticKind::FloatSub:
                if (callbacks_.sub_f64) callbacks_.sub_f64(inst, out); return;
            case machine::MachineSemanticKind::FloatMul:
                if (callbacks_.mul_f64) callbacks_.mul_f64(inst, out); return;
            case machine::MachineSemanticKind::FloatDiv:
                if (callbacks_.div_f64) callbacks_.div_f64(inst, out); return;
            default:
                if (callbacks_.fail_emission) callbacks_.fail_emission("Unhandled ROCm FloatBinary64 opcode", static_cast<uint32_t>(inst.opcode)); return;
        }
    }

    inline void lower_float_binary64(const lowering::SelectedInstruction& selected, ByteStream& out) {
        machine::MachineInstruction inst;
        inst.form = selected.form;
        inst.semantic_kind = selected.semantic_kind;
        lower_float_binary64(inst, out);
    }

    inline void lower_compare32(const machine::MachineInstruction& inst, ByteStream& out) {
        switch (inst.semantic_kind) {
            case machine::MachineSemanticKind::Compare:
                if (callbacks_.cmp_i32) callbacks_.cmp_i32(inst, out); return;
            default:
                if (callbacks_.fail_emission) callbacks_.fail_emission("Unhandled ROCm Compare32 opcode", static_cast<uint32_t>(inst.opcode)); return;
        }
    }

    inline void lower_compare32(const lowering::SelectedInstruction& selected, ByteStream& out) {
        machine::MachineInstruction inst;
        inst.form = selected.form;
        inst.semantic_kind = selected.semantic_kind;
        lower_compare32(inst, out);
    }

    inline void lower_vector_binary512(const machine::MachineInstruction& inst, ByteStream& out) {
        switch (inst.semantic_kind) {
            case machine::MachineSemanticKind::VectorAdd:
                if (callbacks_.vec_add_f32_512) callbacks_.vec_add_f32_512(inst, out); return;
            case machine::MachineSemanticKind::VectorMul:
                if (callbacks_.vec_mul_f32_512) callbacks_.vec_mul_f32_512(inst, out); return;
            default:
                if (callbacks_.fail_emission) callbacks_.fail_emission("Unhandled ROCm VectorBinary512 opcode", static_cast<uint32_t>(inst.opcode)); return;
        }
    }

    inline void lower_vector_binary512(const lowering::SelectedInstruction& selected, ByteStream& out) {
        machine::MachineInstruction inst;
        inst.form = selected.form;
        inst.semantic_kind = selected.semantic_kind;
        lower_vector_binary512(inst, out);
    }

private:
    EncoderCallbacks callbacks_;
};

} // namespace dgtl::isa::lowering

export namespace dgtl::isa::amdgpu {

class ROCmTargetLoweringContract {
public:
    using MachineOpcode = machine::MachineOpcode;

    struct Result {
        MachineOpcode opcode = machine::INVALID_MACHINE_OPCODE;
        bool assigned = false;
        const char* diagnostic = nullptr;
    };

    [[nodiscard]] static Result assign_opcode(
        const machine::MachineInstruction& instruction) noexcept {
        const auto op = dgtl::isa::amdgpu::opcode_for(instruction.form, instruction.semantic_kind);
        if (op != machine::GM_NOOP || instruction.form == machine::MachineForm::NoOp) {
            return Result{op, true, nullptr};
        }
        return Result{
            machine::INVALID_MACHINE_OPCODE,
            false,
            "ROCm target lowering unhandled MachineForm or SemanticKind"};
    }
};

class ROCmMIRBoundary {
public:
    [[nodiscard]] static bool lower(
        machine::MachineFunction& function,
        std::string& diagnostic) {

        diagnostic.clear();
        const auto pre_verification =
            machine::MachineVerifier::verify(function);

        if (!pre_verification.ok()) {
            diagnostic = "MachineVerifier rejected pre-lowering ROCm MIR";
            return false;
        }

        for (std::size_t b = 0; b < function.blocks().size(); ++b) {
            auto* block = function.block(static_cast<machine::BlockId>(b));
            if (!block) continue;
            for (auto inst_id : block->instructions) {
                auto* inst = function.instruction(inst_id);
                if (!inst) continue;
                if (inst->opcode == machine::INVALID_MACHINE_OPCODE || inst->opcode == machine::GM_NOOP) {
                    const auto result = ROCmTargetLoweringContract::assign_opcode(*inst);
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

        ROCmTargetRegisterInfo reg_info{};
        dgtl::mir::regalloc::UniversalGraphColoringRegAlloc allocator(function, reg_info);
        if (!allocator.run()) {
            diagnostic = "ROCm universal register allocation failed";
            return false;
        }

        std::vector<machine::MachineRegister> callee_saved;
        for (const auto& instruction : function.instructions()) {
            for (const auto& operand : instruction.operands) {
                if (operand.kind != machine::MachineOperandKind::Register ||
                    !operand.reg.is_physical()) {
                    continue;
                }

                const bool sgpr_saved =
                    (operand.reg.reg_class ==
                         machine::RegisterClassKind::General ||
                     operand.reg.reg_class ==
                         machine::RegisterClassKind::Integer ||
                     operand.reg.reg_class ==
                         machine::RegisterClassKind::Address) &&
                    ROCmFrameCalculator::is_callee_saved_sgpr(operand.reg.id);

                if (sgpr_saved) {
                    if (std::find_if(
                            callee_saved.begin(), callee_saved.end(),
                            [&](const auto& x) {
                                return x.id == operand.reg.id &&
                                       x.reg_class == operand.reg.reg_class;
                            }) == callee_saved.end()) {
                        callee_saved.push_back(operand.reg);
                    }
                }
            }
        }

        if (!ROCmFrameCalculator::apply_machine_function(function, callee_saved)) {
            diagnostic = "ROCm scratch frame lowering failed";
            return false;
        }

        const auto verification =
            machine::MachineVerifier::verify(function);

        if (!verification.ok()) {
            diagnostic =
                "MachineVerifier rejected frame-lowered ROCm MIR";
            return false;
        }

        return true;
    }
};

class ROCmCodeBuffer {
public:
    std::vector<uint32_t> words;

    void emit_u32(uint32_t val) { words.push_back(val); }
};

class ROCmMachineFormLowerer {
public:
    static void emit_prologue(const ROCmFramePlan& /*plan*/, ROCmCodeBuffer& buf) {
        // s_waitcnt vmcnt(0) expcnt(0) lgkmcnt(0) -> 0xBF8C007F
        buf.emit_u32(0xBF8C007F);
    }

    static void emit_epilogue(const ROCmFramePlan& /*plan*/, ROCmCodeBuffer& buf) {
        // s_endpgm -> 0xBF810000
        buf.emit_u32(0xBF810000);
    }
};

inline bool verify_frame_alignment_invariant() {
    for (uint32_t locals = 0; locals < 512; locals += 7) {
        for (uint32_t outgoing = 0; outgoing < 128; outgoing += 8) {
            const auto plan = ROCmFrameCalculator::compute_plan(locals, 0, outgoing, {}, true);
            if ((plan.total_frame_bytes % 16) != 0) {
                return false;
            }
        }
    }
    return true;
}

class ROCmMachineCodeEmitter final : public dgtl::machine::MachineCodeEmitter {
public:
    void emit_instruction(const dgtl::machine::MachineInstruction& inst,
                          std::vector<std::uint8_t>& out) override {
        switch (inst.form) {
            case dgtl::machine::MachineForm::Return: {
                // s_endpgm -> 0xBF810000
                const uint32_t endpgm = 0xBF810000;
                for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>((endpgm >> (i * 8)) & 0xFF));
                break;
            }
            case dgtl::machine::MachineForm::NoOp: {
                // s_nop 0 -> 0xBF800000
                const uint32_t nop = 0xBF800000;
                for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>((nop >> (i * 8)) & 0xFF));
                break;
            }
            default:
                break;
        }
    }
};

} // namespace dgtl::isa::amdgpu