/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Component:   WebAssembly Machine Form Lowering / MIR Construction
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

export module dgtl.isa.wasm.wasm_machine_form_lowering;

import dgtl.mir.isel_gen;
import dgtl.mir.instruction_selector;
import dgtl.mir.runtime_contract;
import dgtl.mir.legalizer;
import dgtl.mir.machine_ir;
import dgtl.isa.wasm.wasm_instruction_info;
import dgtl.isa.wasm.isel_gen;
import dgtl.isa.wasm.frame_lowering;
import dgtl.isa.wasm.reg_map;
import dgtl.mir.regalloc.universal;
import dgtl.target;

export namespace dgtl::isa::lowering {

using MachineForm = dgtl::mir::MachineForm;
using MachineSemanticKind = dgtl::mir::MachineSemanticKind;
using SelectedInstruction = dgtl::mir::isel::SelectedInstruction;
using InstructionSelectorProgram = dgtl::mir::isel::InstructionSelectorProgram;

/**
 * @brief ByteStream representation for Wasm bytecode emission buffers.
 */
using ByteStream = std::vector<std::uint8_t>;

// Encoder method type: pure MIR machine instruction encoder
using EncoderFunc = std::function<void(const machine::MachineInstruction&, ByteStream&)>;

/**
 * @brief Encoder callbacks for Wasm instruction emission.
 */
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

    std::function<void(const char*, std::uint32_t)> fail_emission;
};

/**
 * @brief WasmFormLowering
 *
 * Dispatches MachineInstruction / SelectedInstruction forms and semantic kinds to WebAssembly bytecode emitter routines.
 */
class WasmFormLowering {
public:
    explicit WasmFormLowering(const EncoderCallbacks& cbs) : callbacks_(cbs) {}

    inline void lower_integer_binary32(const machine::MachineInstruction& inst, ByteStream& out) {
        switch (inst.semantic_kind) {
            case machine::MachineSemanticKind::IntegerAdd: if (callbacks_.add_i32) callbacks_.add_i32(inst, out); return;
            case machine::MachineSemanticKind::IntegerSub: if (callbacks_.sub_i32) callbacks_.sub_i32(inst, out); return;
            case machine::MachineSemanticKind::IntegerMul: if (callbacks_.mul_i32) callbacks_.mul_i32(inst, out); return;
            case machine::MachineSemanticKind::IntegerDiv: if (callbacks_.div_i32) callbacks_.div_i32(inst, out); return;
            case machine::MachineSemanticKind::IntegerMod: if (callbacks_.mod_i32) callbacks_.mod_i32(inst, out); return;
            case machine::MachineSemanticKind::IntegerAnd: if (callbacks_.and_i32) callbacks_.and_i32(inst, out); return;
            case machine::MachineSemanticKind::IntegerOr:  if (callbacks_.or_i32)  callbacks_.or_i32(inst, out); return;
            case machine::MachineSemanticKind::IntegerXor: if (callbacks_.xor_i32) callbacks_.xor_i32(inst, out); return;
            case machine::MachineSemanticKind::IntegerShl: if (callbacks_.shl_i32) callbacks_.shl_i32(inst, out); return;
            case machine::MachineSemanticKind::IntegerShr: if (callbacks_.shr_i32) callbacks_.shr_i32(inst, out); return;
            default:
                if (callbacks_.fail_emission) callbacks_.fail_emission("Unhandled Wasm IntegerBinary32 opcode", static_cast<uint32_t>(inst.opcode));
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
            case machine::MachineSemanticKind::IntegerAdd: if (callbacks_.add_i64) callbacks_.add_i64(inst, out); return;
            case machine::MachineSemanticKind::IntegerSub: if (callbacks_.sub_i64) callbacks_.sub_i64(inst, out); return;
            case machine::MachineSemanticKind::IntegerMul: if (callbacks_.mul_i64) callbacks_.mul_i64(inst, out); return;
            case machine::MachineSemanticKind::IntegerDiv: if (callbacks_.div_i64) callbacks_.div_i64(inst, out); return;
            case machine::MachineSemanticKind::IntegerMod: if (callbacks_.mod_i64) callbacks_.mod_i64(inst, out); return;
            case machine::MachineSemanticKind::IntegerAnd: if (callbacks_.and_i64) callbacks_.and_i64(inst, out); return;
            case machine::MachineSemanticKind::IntegerOr:  if (callbacks_.or_i64)  callbacks_.or_i64(inst, out); return;
            case machine::MachineSemanticKind::IntegerXor: if (callbacks_.xor_i64) callbacks_.xor_i64(inst, out); return;
            case machine::MachineSemanticKind::IntegerShl: if (callbacks_.shl_i64) callbacks_.shl_i64(inst, out); return;
            case machine::MachineSemanticKind::IntegerShr: if (callbacks_.shr_i64) callbacks_.shr_i64(inst, out); return;
            default:
                if (callbacks_.fail_emission) callbacks_.fail_emission("Unhandled Wasm IntegerBinary64 opcode", static_cast<uint32_t>(inst.opcode));
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
            case machine::MachineSemanticKind::FloatAdd: if (callbacks_.add_f32) callbacks_.add_f32(inst, out); return;
            case machine::MachineSemanticKind::FloatSub: if (callbacks_.sub_f32) callbacks_.sub_f32(inst, out); return;
            case machine::MachineSemanticKind::FloatMul: if (callbacks_.mul_f32) callbacks_.mul_f32(inst, out); return;
            case machine::MachineSemanticKind::FloatDiv: if (callbacks_.div_f32) callbacks_.div_f32(inst, out); return;
            default:
                if (callbacks_.fail_emission) callbacks_.fail_emission("Unhandled Wasm FloatBinary32 opcode", static_cast<uint32_t>(inst.opcode));
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
            case machine::MachineSemanticKind::FloatAdd: if (callbacks_.add_f64) callbacks_.add_f64(inst, out); return;
            case machine::MachineSemanticKind::FloatSub: if (callbacks_.sub_f64) callbacks_.sub_f64(inst, out); return;
            case machine::MachineSemanticKind::FloatMul: if (callbacks_.mul_f64) callbacks_.mul_f64(inst, out); return;
            case machine::MachineSemanticKind::FloatDiv: if (callbacks_.div_f64) callbacks_.div_f64(inst, out); return;
            default:
                if (callbacks_.fail_emission) callbacks_.fail_emission("Unhandled Wasm FloatBinary64 opcode", static_cast<uint32_t>(inst.opcode));
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
                if (callbacks_.cmp_i32) {
                    callbacks_.cmp_i32(inst, out);
                    return;
                }
                break;
            default:
                break;
        }
        if (callbacks_.cmp_i32) {
            callbacks_.cmp_i32(inst, out);
            return;
        }
        if (callbacks_.fail_emission) {
            callbacks_.fail_emission("Unhandled Wasm Compare32 opcode", static_cast<uint32_t>(inst.opcode));
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
            case machine::MachineSemanticKind::VectorAdd: if (callbacks_.vec_add_f32_512) callbacks_.vec_add_f32_512(inst, out); return;
            case machine::MachineSemanticKind::VectorMul: if (callbacks_.vec_mul_f32_512) callbacks_.vec_mul_f32_512(inst, out); return;
            default:
                if (callbacks_.fail_emission) callbacks_.fail_emission("Unhandled Wasm VectorBinary512 opcode", static_cast<uint32_t>(inst.opcode));
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

} // namespace dgtl::isa::lowering

export namespace dgtl::isa::wasm {

class WasmTargetLoweringContract {
public:
    using MachineOpcode = machine::MachineOpcode;

    struct Result {
        MachineOpcode opcode = machine::INVALID_MACHINE_OPCODE;
        bool assigned = false;
        const char* diagnostic = nullptr;
    };

    [[nodiscard]] static Result assign_opcode(
        const machine::MachineInstruction& instruction) noexcept {
        const auto op = dgtl::isa::wasm::opcode_for(instruction.form, instruction.semantic_kind);
        if (op != machine::GM_NOOP || instruction.form == machine::MachineForm::NoOp) {
            return Result{op, true, nullptr};
        }
        return Result{
            machine::INVALID_MACHINE_OPCODE,
            false,
            "Wasm target lowering unhandled MachineForm or SemanticKind"};
    }
};

class WasmMIRBoundary {
public:
    [[nodiscard]] static bool lower(
        machine::MachineFunction& function,
        std::string& diagnostic) {

        diagnostic.clear();
        const auto pre_verification =
            machine::MachineVerifier::verify(function);

        if (!pre_verification.ok()) {
            diagnostic = "MachineVerifier rejected pre-lowering Wasm MIR";
            return false;
        }

        for (std::size_t b = 0; b < function.blocks().size(); ++b) {
            auto* block = function.block(static_cast<machine::BlockId>(b));
            if (!block) continue;
            for (auto inst_id : block->instructions) {
                auto* inst = function.instruction(inst_id);
                if (!inst) continue;
                if (inst->opcode == machine::INVALID_MACHINE_OPCODE || inst->opcode == machine::GM_NOOP) {
                    const auto result = WasmTargetLoweringContract::assign_opcode(*inst);
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

        WasmTargetRegisterInfo reg_info{};
        dgtl::mir::regalloc::UniversalGraphColoringRegAlloc allocator(function, reg_info);
        if (!allocator.run()) {
            diagnostic = "Wasm universal register allocation failed";
            return false;
        }

        if (!WasmFrameCalculator::apply_machine_function(function, {})) {
            diagnostic = "Wasm linear memory frame lowering failed";
            return false;
        }

        const auto verification =
            machine::MachineVerifier::verify(function);

        if (!verification.ok()) {
            diagnostic = "MachineVerifier rejected frame-lowered Wasm MIR";
            return false;
        }

        return true;
    }
};

class WasmCodeBuffer {
public:
    std::vector<uint8_t> bytes;

    void emit_u8(uint8_t b) { bytes.push_back(b); }
    void emit_leb128_u32(uint32_t val) {
        do {
            uint8_t byte = val & 0x7Fu;
            val >>= 7u;
            if (val != 0) byte |= 0x80u;
            bytes.push_back(byte);
        } while (val != 0);
    }
    void emit_leb128_i32(int32_t val) {
        bool more = true;
        while (more) {
            uint8_t byte = val & 0x7F;
            val >>= 7;
            if ((val == 0 && !(byte & 0x40)) || (val == -1 && (byte & 0x40))) {
                more = false;
            } else {
                byte |= 0x80;
            }
            bytes.push_back(byte);
        }
    }
};

class WasmMachineFormLowerer {
public:
    static void emit_prologue(const WasmFramePlan& plan, WasmCodeBuffer& buf) {
        if (plan.total_frame_bytes > 0) {
            // global.get 0 (stack pointer) -> 0x23 0x00
            buf.emit_u8(0x23); buf.emit_leb128_u32(0);
            // i32.const frame_size -> 0x41
            buf.emit_u8(0x41); buf.emit_leb128_i32(static_cast<int32_t>(plan.total_frame_bytes));
            // i32.sub -> 0x6B
            buf.emit_u8(0x6B);
            // global.set 0 -> 0x24 0x00
            buf.emit_u8(0x24); buf.emit_leb128_u32(0);
        }
    }

    static void emit_epilogue(const WasmFramePlan& plan, WasmCodeBuffer& buf) {
        if (plan.total_frame_bytes > 0) {
            // global.get 0 -> 0x23 0x00
            buf.emit_u8(0x23); buf.emit_leb128_u32(0);
            // i32.const frame_size -> 0x41
            buf.emit_u8(0x41); buf.emit_leb128_i32(static_cast<int32_t>(plan.total_frame_bytes));
            // i32.add -> 0x6A
            buf.emit_u8(0x6A);
            // global.set 0 -> 0x24 0x00
            buf.emit_u8(0x24); buf.emit_leb128_u32(0);
        }
        // end -> 0x0B
        buf.emit_u8(0x0B);
    }
};

inline bool verify_frame_alignment_invariant() {
    for (uint32_t locals = 0; locals < 512; locals += 7) {
        for (uint32_t outgoing = 0; outgoing < 128; outgoing += 8) {
            const auto plan = WasmFrameCalculator::compute_plan(locals, 0, outgoing, {}, true);
            if ((plan.total_frame_bytes % 16) != 0) {
                return false;
            }
        }
    }
    return true;
}

class WasmMachineCodeEmitter final : public dgtl::machine::MachineCodeEmitter {
public:
    void emit_instruction(const dgtl::machine::MachineInstruction& inst,
                          std::vector<std::uint8_t>& out) override {
        switch (inst.form) {
            case dgtl::machine::MachineForm::Return:
                out.push_back(0x0F); // return
                break;
            case dgtl::machine::MachineForm::NoOp:
                out.push_back(0x01); // nop
                break;
            default:
                break;
        }
    }
};

} // namespace dgtl::isa::wasm

export namespace dgtl::isa::lowering {
using namespace dgtl::isa::wasm;
}
