/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:    DGTL (Deep General-purpose Transformation Layer)
 * Owner:      Divyanshu Tata
 * Repository: github.com/TataG-Systems/DGTL
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include <functional>
#include "dgtl/isa/lowering/Instruction_Selector.hpp"
#include "dgtl/isa/lowering/Legalizer.hpp"
#include "dgtl/ir/Global_IR.hpp"
#include "dgtl/isa/regalloc/X64_Reg_Map.hpp"
#include "dgtl/isa/lowering/gen/X64_ISel_Generated.hpp"

namespace dgtl::isa::lowering {

namespace ir = dgtl::ir;

/**
 * @brief X64_Machine_Form_Lowering
 *
 * Handles machine-form lowering: routes selected instructions to target-specific
 * encoding based on generated semantic kind tables. Decouples instruction selection
 * from low-level x64 encoding.
 *
 * The lowering layer queries generated kind tables and dispatches to encoder methods,
 * leaving the emitter as a thin register-state plus byte-encoding shell only.
 */
class X64FormLowering {
public:
    using ByteStream = std::vector<uint8_t>;
    using Emitter = void*; // Forward ref; actual type is X64StrikeEmitter

    // Encoder method type (void (X64StrikeEmitter::*)(const ir::IRNode&, ByteStream&))
    // We use std::function to store these
    using EncoderFunc = std::function<void(const ir::IRNode&, ByteStream&)>;

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

        EncoderFunc add_f32;
        EncoderFunc mul_f32;

        EncoderFunc cmp_i32;

        EncoderFunc vec_add_f32_512;
        EncoderFunc vec_mul_f32_512;

        std::function<void(const char*, uint32_t)> fail_emission;
    };

    explicit X64FormLowering(const EncoderCallbacks& cbs) : callbacks_(cbs) {}

    inline void lower_integer_binary32(const SelectedInstruction& selected, ByteStream& out) {
        switch (generated::integer_binary32_kind(selected.semantic_opcode)) {
            case generated::X64IntegerBinary32Kind::Add:
                callbacks_.add_i32(selected.node, out);
                return;
            case generated::X64IntegerBinary32Kind::Sub:
                callbacks_.sub_i32(selected.node, out);
                return;
            case generated::X64IntegerBinary32Kind::Mul:
                callbacks_.mul_i32(selected.node, out);
                return;
            case generated::X64IntegerBinary32Kind::Div:
                callbacks_.div_i32(selected.node, out);
                return;
            case generated::X64IntegerBinary32Kind::Mod:
                callbacks_.mod_i32(selected.node, out);
                return;
            case generated::X64IntegerBinary32Kind::And:
                callbacks_.and_i32(selected.node, out);
                return;
            case generated::X64IntegerBinary32Kind::Or:
                callbacks_.or_i32(selected.node, out);
                return;
            case generated::X64IntegerBinary32Kind::Xor:
                callbacks_.xor_i32(selected.node, out);
                return;
            case generated::X64IntegerBinary32Kind::Shl:
                callbacks_.shl_i32(selected.node, out);
                return;
            case generated::X64IntegerBinary32Kind::Shr:
                callbacks_.shr_i32(selected.node, out);
                return;
            case generated::X64IntegerBinary32Kind::None:
            default:
                callbacks_.fail_emission("Unhandled MachineForm::IntegerBinary32 opcode",
                                        static_cast<uint32_t>(selected.semantic_opcode));
                return;
        }
    }

    inline void lower_integer_binary64(const SelectedInstruction& selected, ByteStream& out) {
        switch (generated::integer_binary64_kind(selected.semantic_opcode)) {
            case generated::X64IntegerBinary64Kind::Add:
                callbacks_.add_i64(selected.node, out);
                return;
            case generated::X64IntegerBinary64Kind::Sub:
                callbacks_.sub_i64(selected.node, out);
                return;
            case generated::X64IntegerBinary64Kind::Mul:
                callbacks_.mul_i64(selected.node, out);
                return;
            case generated::X64IntegerBinary64Kind::Div:
                callbacks_.div_i64(selected.node, out);
                return;
            case generated::X64IntegerBinary64Kind::Mod:
                callbacks_.mod_i64(selected.node, out);
                return;
            case generated::X64IntegerBinary64Kind::None:
            default:
                callbacks_.fail_emission("Unhandled MachineForm::IntegerBinary64 opcode",
                                        static_cast<uint32_t>(selected.semantic_opcode));
                return;
        }
    }

    inline void lower_float_binary32(const SelectedInstruction& selected, ByteStream& out) {
        switch (generated::float_binary32_kind(selected.semantic_opcode)) {
            case generated::X64FloatBinary32Kind::Add:
                callbacks_.add_f32(selected.node, out);
                return;
            case generated::X64FloatBinary32Kind::Mul:
                callbacks_.mul_f32(selected.node, out);
                return;
            case generated::X64FloatBinary32Kind::None:
            default:
                callbacks_.fail_emission("Unhandled MachineForm::FloatBinary32 opcode",
                                        static_cast<uint32_t>(selected.semantic_opcode));
                return;
        }
    }

    inline void lower_compare32(const SelectedInstruction& selected, ByteStream& out) {
        switch (generated::compare32_kind(selected.semantic_opcode)) {
            case generated::X64Compare32Kind::Any:
                callbacks_.cmp_i32(selected.node, out);
                return;
            case generated::X64Compare32Kind::None:
            default:
                callbacks_.fail_emission("Unhandled MachineForm::Compare32 opcode",
                                        static_cast<uint32_t>(selected.semantic_opcode));
                return;
        }
    }

    inline void lower_vector_binary512(const SelectedInstruction& selected, ByteStream& out) {
        switch (generated::vector_binary512_kind(selected.semantic_opcode)) {
            case generated::X64VectorBinary512Kind::Add:
                callbacks_.vec_add_f32_512(selected.node, out);
                return;
            case generated::X64VectorBinary512Kind::Mul:
                callbacks_.vec_mul_f32_512(selected.node, out);
                return;
            case generated::X64VectorBinary512Kind::None:
            default:
                callbacks_.fail_emission("Unhandled MachineForm::VectorBinary512 opcode",
                                        static_cast<uint32_t>(selected.semantic_opcode));
                return;
        }
    }

private:
    EncoderCallbacks callbacks_;
};

} // namespace dgtl::isa::lowering

namespace dgtl::isa {

class X64StrikeEmitter {
public:
    X64StrikeEmitter()
                : target_machine_(core::ArchType::X86_64_AVX2),
          selector_(lowering::make_builtin_instruction_selector_program()) {}

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

    [[nodiscard]] ir::OpCode last_failed_opcode() const noexcept {
        return last_failed_opcode_;
    }

    [[nodiscard]] bool process_stream(const ir::IRStream& stream, std::vector<uint8_t>& out) const {
        out.clear();
        had_error_ = false;
        last_error_.clear();
        last_failed_opcode_ = ir::OpCode::NOP;

        const auto legalized = legalizer_.legalize(stream, target_machine_);
        const ir::IRStream& machine_stream = legalized.stream;

        for (size_t i = 0; i < machine_stream.size(); ++i) {
            const ir::IRNode node = machine_stream.get_node(i);
            const auto selected = selector_.select_instruction(machine_stream, i, node, target_machine_);

            switch (selected.form) {
                case lowering::MachineForm::Move:
                    emit_move(selected.node, out);
                    break;
                case lowering::MachineForm::LoadImmediate:
                    emit_load_imm(selected.node, out);
                    break;
                case lowering::MachineForm::IntegerBinary32:
                    emit_i32_binary(selected.node, out);
                    break;
                case lowering::MachineForm::Call:
                    emit_call_slot(selected.node, out);
                    break;
                case lowering::MachineForm::Return:
                    out.push_back(0xC3);
                    break;
                case lowering::MachineForm::NoOp:
                case lowering::MachineForm::Unhandled:
                default:
                    mark_error("Unhandled machine form", selected.node.opcode);
                    out.push_back(0x90);
                    break;
            }
        }

        if (out.empty() || out.back() != 0xC3) {
            out.push_back(0xC3);
        }
        return true;
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

    static uint8_t map_reg(uint32_t vreg) {
        return static_cast<uint8_t>(exec::X64SinglePassRegisterMapper::map_gpr_branchless(vreg));
    }

    static uint64_t combine_u64_from_ops(const ir::IRNode& node) {
        return (static_cast<uint64_t>(node.op_b) << 32u) | static_cast<uint64_t>(node.op_a);
    }

    static void emit_load_imm(const ir::IRNode& node, std::vector<uint8_t>& out) {
        const uint8_t dst = map_reg(node.result_id);
        out.push_back(exec::X64Encoding::rex(/*w=*/true, /*r=*/false, /*x=*/false, /*b=*/(dst >= 8u)));
        out.push_back(static_cast<uint8_t>(0xB8u + (dst & 0x7u)));
        append_u64(out, combine_u64_from_ops(node));
    }

    static void emit_move(const ir::IRNode& node, std::vector<uint8_t>& out) {
        const uint8_t src = map_reg(node.op_a);
        const uint8_t dst = map_reg(node.result_id);

        out.push_back(exec::X64Encoding::rex(/*w=*/true, /*r=*/(src >= 8u), /*x=*/false, /*b=*/(dst >= 8u)));
        out.push_back(0x89); // MOV r/m64, r64
        emit_modrm_reg_reg(out, src, dst);
    }

    static void emit_i32_binary(const ir::IRNode& node, std::vector<uint8_t>& out) {
        const uint8_t lhs = map_reg(node.op_a);
        const uint8_t rhs = map_reg(node.op_b);

        out.push_back(exec::X64Encoding::rex(/*w=*/false, /*r=*/(rhs >= 8u), /*x=*/false, /*b=*/(lhs >= 8u)));
        switch (node.opcode) {
            case ir::OpCode::ADD_I32:
                out.push_back(0x01); // ADD r/m32, r32
                emit_modrm_reg_reg(out, rhs, lhs);
                break;
            case ir::OpCode::SUB_I32:
                out.push_back(0x29); // SUB r/m32, r32
                emit_modrm_reg_reg(out, rhs, lhs);
                break;
            case ir::OpCode::MUL_I32:
                out.push_back(0x0F);
                out.push_back(0xAF); // IMUL r32, r/m32
                emit_modrm_reg_reg(out, lhs, rhs);
                break;
            default:
                out.push_back(0x90);
                break;
        }
    }

    void emit_call_slot(const ir::IRNode& node, std::vector<uint8_t>& out) const {
        const auto it = runtime_slots_.find(node.op_a);
        if (it == runtime_slots_.end()) {
            mark_error("Missing runtime function slot", node.opcode);
            out.push_back(0x90);
            return;
        }

        // movabs rax, imm64
        out.push_back(exec::X64Encoding::rex(/*w=*/true, /*r=*/false, /*x=*/false, /*b=*/false));
        out.push_back(0xB8);
        append_u64(out, static_cast<uint64_t>(it->second));

        // call rax
        out.push_back(0xFF);
        out.push_back(0xD0);
    }

    void mark_error(const char* msg, ir::OpCode opcode) const {
        if (!had_error_) {
            had_error_ = true;
            last_error_ = msg;
            last_failed_opcode_ = opcode;
        }
    }

private:
    core::TargetMachine target_machine_;
    mutable lowering::MachineLegalizer legalizer_{};
    lowering::InstructionSelectorProgram selector_;
    std::unordered_map<std::string, uintptr_t> runtime_functions_;
    std::unordered_map<uint32_t, uintptr_t> runtime_slots_;
    mutable bool had_error_ = false;
    mutable std::string last_error_;
    mutable ir::OpCode last_failed_opcode_ = ir::OpCode::NOP;
};

} // namespace dgtl::isa
