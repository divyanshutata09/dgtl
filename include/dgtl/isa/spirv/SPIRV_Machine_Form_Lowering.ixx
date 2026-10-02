/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Component:   SPIR-V Machine Form Lowering & MIR Transformation
 * Standards:   C++26 / Clang 18+ / MSVC 19.40+ / GCC 14+
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
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

export module dgtl.isa.spirv.machine_form_lowering;

import dgtl.mir.instruction_selector;
import dgtl.mir.runtime_contract;
import dgtl.mir.legalizer;
import dgtl.mir.isel_gen;
import dgtl.mir.machine_ir;
import dgtl.isa.spirv.isel_gen;
import dgtl.isa.spirv.instruction_info;
import dgtl.isa.spirv.frame_lowering;
import dgtl.isa.spirv.reg_map;
import dgtl.mir.regalloc.universal;
import dgtl.target;

export namespace dgtl::isa::lowering {

using SelectedInstruction = dgtl::mir::isel::SelectedInstruction;
using InstructionSelectorProgram = dgtl::mir::isel::InstructionSelectorProgram;

/**
 * @brief SPIRVFormLowering
 *
 * Dispatches SelectedInstruction forms and semantic kinds to SPIR-V encoder routines.
 */
class SPIRVFormLowering {
public:
    using ByteStream = std::vector<uint8_t>;
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

        EncoderFunc add_f32;
        EncoderFunc sub_f32;
        EncoderFunc mul_f32;
        EncoderFunc div_f32;

        EncoderFunc cmp_i32;

        std::function<void(const char*, uint32_t)> fail_emission;
    };

    explicit SPIRVFormLowering(const EncoderCallbacks& cbs) : callbacks_(cbs) {}

    inline void lower_integer_binary32(const machine::MachineInstruction& inst,
                                       ByteStream& out) {
        switch (inst.semantic_kind) {
            case machine::MachineSemanticKind::IntegerAdd:
                if (callbacks_.add_i32) callbacks_.add_i32(inst, out);
                return;
            case machine::MachineSemanticKind::IntegerSub:
                if (callbacks_.sub_i32) callbacks_.sub_i32(inst, out);
                return;
            case machine::MachineSemanticKind::IntegerMul:
                if (callbacks_.mul_i32) callbacks_.mul_i32(inst, out);
                return;
            case machine::MachineSemanticKind::IntegerDiv:
                if (callbacks_.div_i32) callbacks_.div_i32(inst, out);
                return;
            case machine::MachineSemanticKind::IntegerMod:
                if (callbacks_.mod_i32) callbacks_.mod_i32(inst, out);
                return;
            case machine::MachineSemanticKind::IntegerAnd:
                if (callbacks_.and_i32) callbacks_.and_i32(inst, out);
                return;
            case machine::MachineSemanticKind::IntegerOr:
                if (callbacks_.or_i32) callbacks_.or_i32(inst, out);
                return;
            case machine::MachineSemanticKind::IntegerXor:
                if (callbacks_.xor_i32) callbacks_.xor_i32(inst, out);
                return;
            case machine::MachineSemanticKind::IntegerShl:
                if (callbacks_.shl_i32) callbacks_.shl_i32(inst, out);
                return;
            case machine::MachineSemanticKind::IntegerShr:
                if (callbacks_.shr_i32) callbacks_.shr_i32(inst, out);
                return;
            default:
                if (callbacks_.fail_emission) {
                    callbacks_.fail_emission("Unhandled SPIR-V IntegerBinary32 opcode",
                                             static_cast<uint32_t>(inst.opcode));
                }
                return;
        }
    }

    inline void lower_integer_binary32(const SelectedInstruction& selected,
                                       ByteStream& out) {
        machine::MachineInstruction inst;
        inst.form = selected.form;
        inst.semantic_kind = selected.semantic_kind;
        lower_integer_binary32(inst, out);
    }

    inline void lower_float_binary32(const machine::MachineInstruction& inst,
                                     ByteStream& out) {
        switch (inst.semantic_kind) {
            case machine::MachineSemanticKind::FloatAdd:
                if (callbacks_.add_f32) callbacks_.add_f32(inst, out);
                return;
            case machine::MachineSemanticKind::FloatSub:
                if (callbacks_.sub_f32) callbacks_.sub_f32(inst, out);
                return;
            case machine::MachineSemanticKind::FloatMul:
                if (callbacks_.mul_f32) callbacks_.mul_f32(inst, out);
                return;
            case machine::MachineSemanticKind::FloatDiv:
                if (callbacks_.div_f32) callbacks_.div_f32(inst, out);
                return;
            default:
                if (callbacks_.fail_emission) {
                    callbacks_.fail_emission("Unhandled SPIR-V FloatBinary32 opcode",
                                             static_cast<uint32_t>(inst.opcode));
                }
                return;
        }
    }

    inline void lower_float_binary32(const SelectedInstruction& selected,
                                     ByteStream& out) {
        machine::MachineInstruction inst;
        inst.form = selected.form;
        inst.semantic_kind = selected.semantic_kind;
        lower_float_binary32(inst, out);
    }

    inline void lower_compare32(const machine::MachineInstruction& inst,
                                ByteStream& out) {
        switch (inst.semantic_kind) {
            case machine::MachineSemanticKind::Compare:
                if (callbacks_.cmp_i32) callbacks_.cmp_i32(inst, out);
                return;
            default:
                switch (inst.comparison_predicate()) {
                    case machine::ComparisonPredicate::Equal:
                    case machine::ComparisonPredicate::NotEqual:
                    case machine::ComparisonPredicate::SignedLessThan:
                    case machine::ComparisonPredicate::SignedLessEqual:
                    case machine::ComparisonPredicate::SignedGreaterThan:
                    case machine::ComparisonPredicate::SignedGreaterEqual:
                    case machine::ComparisonPredicate::UnsignedLessThan:
                    case machine::ComparisonPredicate::UnsignedLessEqual:
                    case machine::ComparisonPredicate::UnsignedGreaterThan:
                    case machine::ComparisonPredicate::UnsignedGreaterEqual:
                    case machine::ComparisonPredicate::Ordered:
                    case machine::ComparisonPredicate::Unordered:
                        if (callbacks_.cmp_i32) callbacks_.cmp_i32(inst, out);
                        return;
                    default:
                        break;
                }
                if (callbacks_.fail_emission) {
                    callbacks_.fail_emission("Unhandled SPIR-V Compare32 opcode",
                                             static_cast<uint32_t>(inst.opcode));
                }
                return;
        }
    }

private:
    EncoderCallbacks callbacks_;
};

} // namespace dgtl::isa::lowering

export namespace dgtl::isa::spirv {

class SPIRVMIRBoundary {
public:
    [[nodiscard]] static bool lower(
        machine::MachineFunction& function,
        std::string& diagnostic) {

        diagnostic.clear();
        const auto pre_verification =
            machine::MachineVerifier::verify(function);

        if (!pre_verification.ok()) {
            diagnostic = "MachineVerifier rejected pre-lowering SPIR-V MIR";
            return false;
        }

        for (std::size_t b = 0; b < function.blocks().size(); ++b) {
            auto* block = function.block(static_cast<machine::BlockId>(b));
            if (!block) continue;
            for (auto inst_id : block->instructions) {
                auto* inst = function.instruction(inst_id);
                if (!inst) continue;
                if (inst->opcode == machine::INVALID_MACHINE_OPCODE || inst->opcode == machine::GM_NOOP) {
                    const auto op = dgtl::isa::spirv::opcode_for(inst->form, inst->semantic_kind);
                    if (op != machine::GM_NOOP || inst->form == machine::MachineForm::NoOp) {
                        inst->opcode = op;
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

    [[nodiscard]] static bool build_and_verify(
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

        SPIRVTargetRegisterInfo reg_info{};
        dgtl::mir::regalloc::UniversalGraphColoringRegAlloc allocator(function, reg_info);
        if (!allocator.run()) {
            diagnostic = "SPIR-V universal register allocation failed";
            return false;
        }

        if (!SPIRVFrameCalculator::apply_machine_function(function, {})) {
            diagnostic = "SPIR-V frame lowering failed";
            return false;
        }

        const auto verification =
            machine::MachineVerifier::verify(function);

        if (!verification.ok()) {
            diagnostic = "MachineVerifier rejected frame-lowered SPIR-V MIR";
            return false;
        }

        return true;
    }
};

} // namespace dgtl::isa::spirv

export namespace dgtl::isa::lowering {
using namespace dgtl::isa::spirv;
using SPIRVMIRBoundary = dgtl::isa::spirv::SPIRVMIRBoundary;
}
