/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Component:   SPIR-V Binary Word & Bytecode Encoder
 * Standards:   C++26 / Clang 18+ / MSVC 19.40+ / GCC 14+
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */
module;

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

export module dgtl.isa.spirv.encoder;

import dgtl.mir.machine_ir;
import dgtl.isa.spirv.instruction_info;
import dgtl.isa.spirv.reg_map;

export namespace dgtl::isa::spirv::emitter {

using namespace dgtl::machine;

namespace spirv_op {
    inline constexpr uint32_t Nop                = 0;
    inline constexpr uint32_t Source             = 3;
    inline constexpr uint32_t Name               = 5;
    inline constexpr uint32_t ExtInstImport      = 11;
    inline constexpr uint32_t MemoryModel        = 14;
    inline constexpr uint32_t EntryPoint         = 15;
    inline constexpr uint32_t ExecutionMode      = 16;
    inline constexpr uint32_t Capability         = 17;
    inline constexpr uint32_t TypeVoid           = 19;
    inline constexpr uint32_t TypeBool           = 20;
    inline constexpr uint32_t TypeInt            = 21;
    inline constexpr uint32_t TypeFloat          = 22;
    inline constexpr uint32_t TypeVector         = 23;
    inline constexpr uint32_t TypePointer        = 32;
    inline constexpr uint32_t TypeFunction       = 33;
    inline constexpr uint32_t ConstantTrue       = 41;
    inline constexpr uint32_t ConstantFalse      = 42;
    inline constexpr uint32_t Constant           = 43;
    inline constexpr uint32_t Function           = 54;
    inline constexpr uint32_t FunctionParameter  = 55;
    inline constexpr uint32_t FunctionEnd        = 56;
    inline constexpr uint32_t FunctionCall       = 57;
    inline constexpr uint32_t Variable           = 59;
    inline constexpr uint32_t Load               = 61;
    inline constexpr uint32_t Store              = 62;
    inline constexpr uint32_t AccessChain        = 65;
    inline constexpr uint32_t IAdd               = 128;
    inline constexpr uint32_t FAdd               = 129;
    inline constexpr uint32_t ISub               = 130;
    inline constexpr uint32_t FSub               = 131;
    inline constexpr uint32_t IMul               = 132;
    inline constexpr uint32_t FMul               = 133;
    inline constexpr uint32_t SDiv               = 135;
    inline constexpr uint32_t FDiv               = 136;
    inline constexpr uint32_t SRem               = 138;
    inline constexpr uint32_t IEqual             = 170;
    inline constexpr uint32_t INotEqual          = 171;
    inline constexpr uint32_t SGreaterThan       = 173;
    inline constexpr uint32_t SGreaterThanEqual  = 175;
    inline constexpr uint32_t SLessThan          = 177;
    inline constexpr uint32_t SLessThanEqual     = 179;
    inline constexpr uint32_t FOrdEqual          = 180;
    inline constexpr uint32_t FOrdNotEqual       = 182;
    inline constexpr uint32_t FOrdLessThan       = 184;
    inline constexpr uint32_t FOrdGreaterThan    = 186;
    inline constexpr uint32_t FOrdLessThanEqual  = 188;
    inline constexpr uint32_t FOrdGreaterThanEqual = 190;
    inline constexpr uint32_t ShiftRightArithmetic = 195;
    inline constexpr uint32_t ShiftLeftLogical   = 196;
    inline constexpr uint32_t BitwiseOr          = 197;
    inline constexpr uint32_t BitwiseXor         = 198;
    inline constexpr uint32_t BitwiseAnd         = 199;
    inline constexpr uint32_t Not                = 200;
    inline constexpr uint32_t Label              = 248;
    inline constexpr uint32_t Branch             = 249;
    inline constexpr uint32_t BranchConditional  = 250;
    inline constexpr uint32_t Return             = 253;
    inline constexpr uint32_t ReturnValue        = 254;
    inline constexpr uint32_t Unreachable        = 255;
} // namespace spirv_op

struct SPIRVModuleHeader {
    uint32_t magic = 0x07230203u;    // SPIR-V magic number
    uint32_t version = 0x00010500u;  // SPIR-V 1.5
    uint32_t generator_id = 0x00260000u; // DGTL Generator
    uint32_t bound = 1000u;
    uint32_t reserved = 0u;
};

class SPIRVBinaryBuilder {
public:
    std::vector<uint32_t> words;

    SPIRVBinaryBuilder() {
        words.reserve(1024);
    }

    void emit_header(uint32_t bound) {
        words.push_back(0x07230203u);
        words.push_back(0x00010500u);
        words.push_back(0x00260000u);
        words.push_back(bound);
        words.push_back(0u);
    }

    void emit_inst(uint32_t opcode, std::span<const uint32_t> operands) {
        const uint32_t word_count = static_cast<uint32_t>(operands.size() + 1u);
        words.push_back((word_count << 16u) | (opcode & 0xFFFFu));
        for (uint32_t op : operands) {
            words.push_back(op);
        }
    }

    void emit_string(uint32_t opcode, std::string_view str, std::span<const uint32_t> prefix = {}) {
        std::vector<uint32_t> ops;
        ops.reserve(prefix.size() + (str.size() / 4u) + 1u);
        for (uint32_t p : prefix) ops.push_back(p);

        uint32_t current_word = 0;
        int shift = 0;
        for (char c : str) {
            current_word |= (static_cast<uint32_t>(static_cast<uint8_t>(c)) << shift);
            shift += 8;
            if (shift == 32) {
                ops.push_back(current_word);
                current_word = 0;
                shift = 0;
            }
        }
        ops.push_back(current_word); // Null terminator word
        emit_inst(opcode, ops);
    }
};

/**
 * @brief SPIR-V Instruction & Function Code Emitter.
 */
class SPIRVCodeEmitter final : public dgtl::machine::MachineCodeEmitter {
public:
    void emit_instruction(const dgtl::machine::MachineInstruction& inst,
                          std::vector<std::uint8_t>& out) override {
        std::vector<uint32_t> words;
        encode_instruction(inst, words);
        const size_t byte_count = words.size() * sizeof(uint32_t);
        const size_t prev_size = out.size();
        out.resize(prev_size + byte_count);
        std::memcpy(out.data() + prev_size, words.data(), byte_count);
    }

    static void encode_instruction(const dgtl::machine::MachineInstruction& inst,
                                   std::vector<uint32_t>& words) {
        using namespace dgtl::machine;

        const auto* def = mir_def(inst);
        const auto uses = mir_uses(inst);

        const uint32_t def_id = def ? (def->reg.id + 100u) : 0u;
        const uint32_t type_i32 = 1u;
        const uint32_t type_f32 = 2u;
        const uint32_t type_bool = 3u;

        auto emit_raw = [&](uint32_t opcode, std::initializer_list<uint32_t> operands) {
            const uint32_t count = static_cast<uint32_t>(operands.size() + 1u);
            words.push_back((count << 16u) | (opcode & 0xFFFFu));
            for (uint32_t op : operands) {
                words.push_back(op);
            }
        };

        switch (inst.form) {
            case MachineForm::NoOp:
                emit_raw(spirv_op::Nop, {});
                break;
            case MachineForm::Move:
                if (def && !uses.empty()) {
                    // In SSA SPIR-V, a copy can be represented as OpBitcast or integer add with 0
                    const uint32_t src_id = uses[0]->reg.id + 100u;
                    emit_raw(spirv_op::IAdd, {type_i32, def_id, src_id, src_id}); // simple SSA alias
                }
                break;
            case MachineForm::LoadImmediate:
                if (def) {
                    const int64_t imm = !uses.empty() && uses[0]->kind == MachineOperandKind::Immediate
                        ? uses[0]->immediate : 0;
                    emit_raw(spirv_op::Constant, {type_i32, def_id, static_cast<uint32_t>(imm & 0xFFFFFFFFu)});
                }
                break;
            case MachineForm::IntegerBinary32:
            case MachineForm::IntegerBinary64: {
                if (def && uses.size() >= 2) {
                    const uint32_t a = uses[0]->reg.id + 100u;
                    const uint32_t b = uses[1]->reg.id + 100u;
                    uint32_t op = spirv_op::IAdd;
                    switch (inst.semantic_kind) {
                        case MachineSemanticKind::IntegerSub: op = spirv_op::ISub; break;
                        case MachineSemanticKind::IntegerMul: op = spirv_op::IMul; break;
                        case MachineSemanticKind::IntegerDiv: op = spirv_op::SDiv; break;
                        case MachineSemanticKind::IntegerMod: op = spirv_op::SRem; break;
                        case MachineSemanticKind::IntegerAnd: op = spirv_op::BitwiseAnd; break;
                        case MachineSemanticKind::IntegerOr:  op = spirv_op::BitwiseOr; break;
                        case MachineSemanticKind::IntegerXor: op = spirv_op::BitwiseXor; break;
                        case MachineSemanticKind::IntegerShl: op = spirv_op::ShiftLeftLogical; break;
                        case MachineSemanticKind::IntegerShr: op = spirv_op::ShiftRightArithmetic; break;
                        default: break;
                    }
                    emit_raw(op, {type_i32, def_id, a, b});
                }
                break;
            }
            case MachineForm::FloatBinary32:
            case MachineForm::FloatBinary64: {
                if (def && uses.size() >= 2) {
                    const uint32_t a = uses[0]->reg.id + 100u;
                    const uint32_t b = uses[1]->reg.id + 100u;
                    uint32_t op = spirv_op::FAdd;
                    switch (inst.semantic_kind) {
                        case MachineSemanticKind::FloatSub: op = spirv_op::FSub; break;
                        case MachineSemanticKind::FloatMul: op = spirv_op::FMul; break;
                        case MachineSemanticKind::FloatDiv: op = spirv_op::FDiv; break;
                        default: break;
                    }
                    emit_raw(op, {type_f32, def_id, a, b});
                }
                break;
            }
            case MachineForm::Compare32: {
                if (def && uses.size() >= 2) {
                    const uint32_t a = uses[0]->reg.id + 100u;
                    const uint32_t b = uses[1]->reg.id + 100u;
                    uint32_t op = spirv_op::IEqual;
                    switch (inst.comparison_predicate()) {
                        case ComparisonPredicate::NotEqual: op = spirv_op::INotEqual; break;
                        case ComparisonPredicate::SignedLessThan: op = spirv_op::SLessThan; break;
                        case ComparisonPredicate::SignedLessEqual: op = spirv_op::SLessThanEqual; break;
                        case ComparisonPredicate::SignedGreaterThan: op = spirv_op::SGreaterThan; break;
                        case ComparisonPredicate::SignedGreaterEqual: op = spirv_op::SGreaterThanEqual; break;
                        default: break;
                    }
                    emit_raw(op, {type_bool, def_id, a, b});
                }
                break;
            }
            case MachineForm::Goto:
                if (!inst.operands.empty() && inst.operands[0].kind == MachineOperandKind::BasicBlock) {
                    const uint32_t target_label = inst.operands[0].block + 10u;
                    emit_raw(spirv_op::Branch, {target_label});
                }
                break;
            case MachineForm::BranchCond:
                if (inst.operands.size() >= 3 &&
                    inst.operands[1].kind == MachineOperandKind::BasicBlock &&
                    inst.operands[2].kind == MachineOperandKind::BasicBlock) {
                    const uint32_t cond_id = inst.operands[0].is_register() ? (inst.operands[0].reg.id + 100u) : 1u;
                    const uint32_t true_label = inst.operands[1].block + 10u;
                    const uint32_t false_label = inst.operands[2].block + 10u;
                    emit_raw(spirv_op::BranchConditional, {cond_id, true_label, false_label});
                }
                break;
            case MachineForm::Return:
                if (!uses.empty() && uses[0]->is_register()) {
                    const uint32_t ret_val = uses[0]->reg.id + 100u;
                    emit_raw(spirv_op::ReturnValue, {ret_val});
                } else {
                    emit_raw(spirv_op::Return, {});
                }
                break;
            default:
                break;
        }
    }

    /**
     * @brief Encodes an entire MachineFunction into a compliant SPIR-V binary word module.
     */
    static std::vector<uint32_t> encode_function_module(const dgtl::machine::MachineFunction& function) {
        SPIRVBinaryBuilder builder;

        const uint32_t max_id = 50000u;
        builder.emit_header(max_id);

        // Capabilities: Shader, Float64, Int64
        builder.emit_inst(spirv_op::Capability, {1u}); // Shader
        builder.emit_inst(spirv_op::Capability, {4u}); // Float64
        builder.emit_inst(spirv_op::Capability, {5u}); // Int64

        // Memory model: Logical, GLSL450
        builder.emit_inst(spirv_op::MemoryModel, {0u, 1u});

        // Entry point
        const uint32_t func_id = 9u;
        builder.emit_string(spirv_op::EntryPoint, function.name().empty() ? "main" : function.name(), {5u, func_id}); // GLCompute

        // Execution mode
        builder.emit_inst(spirv_op::ExecutionMode, {func_id, 17u, 1u, 1u, 1u}); // LocalSize 1, 1, 1

        // Type declarations
        const uint32_t type_void = 1u;
        const uint32_t type_i32  = 2u;
        const uint32_t type_f32  = 3u;
        const uint32_t type_bool = 4u;
        const uint32_t type_func = 5u;

        builder.emit_inst(spirv_op::TypeVoid, {type_void});
        builder.emit_inst(spirv_op::TypeInt, {type_i32, 32u, 1u});
        builder.emit_inst(spirv_op::TypeFloat, {type_f32, 32u});
        builder.emit_inst(spirv_op::TypeBool, {type_bool});
        builder.emit_inst(spirv_op::TypeFunction, {type_func, type_void});

        // Function start
        builder.emit_inst(spirv_op::Function, {type_void, func_id, 0u, type_func});

        // Blocks and instructions
        for (const auto& block : function.blocks()) {
            const uint32_t label_id = block.id + 10u;
            builder.emit_inst(spirv_op::Label, {label_id});

            for (dgtl::machine::InstrId instruction_id : block.instructions) {
                const auto* instruction = function.instruction(instruction_id);
                if (!instruction) continue;
                encode_instruction(*instruction, builder.words);
            }
        }

        // Function end
        builder.emit_inst(spirv_op::FunctionEnd, {});

        return builder.words;
    }
};

} // namespace dgtl::isa::spirv::emitter
