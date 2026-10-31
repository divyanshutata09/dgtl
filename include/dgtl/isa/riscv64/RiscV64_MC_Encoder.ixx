/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Component:   RISC-V 64 (RV64GC) Standalone Machine Code Binary Encoder
 * Standards:   C++26 / Clang 18+ / MSVC 19.40+ / GCC 14+
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

module;

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

export module dgtl.isa.riscv64.mc_encoder;

import dgtl.mir.machine_ir;
import dgtl.mir.runtime_contract;
import dgtl.object.file;
import dgtl.object.section;
import dgtl.object.symbol;
import dgtl.object.relocation;
import dgtl.isa.riscv64.riscv64_instruction_info;
import dgtl.isa.riscv64.riscv64_reg_map;

export namespace dgtl::isa::riscv64 {

using namespace dgtl::machine;
using namespace dgtl::object;

struct RiscV64BranchPatch {
    std::size_t patch_offset = 0;
    BlockId target_block = INVALID_BLOCK_ID;
    bool is_jal = false;
    uint32_t funct3 = 0;
    uint32_t rs1 = 0;
    uint32_t rs2 = 0;
};

class RiscV64MCEncoder {
public:
    static void emit_u32(Section& sec, uint32_t val) {
        sec.bytes.push_back(static_cast<uint8_t>(val & 0xFF));
        sec.bytes.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
        sec.bytes.push_back(static_cast<uint8_t>((val >> 16) & 0xFF));
        sec.bytes.push_back(static_cast<uint8_t>((val >> 24) & 0xFF));
    }

    // R-type encoding
    static constexpr uint32_t encode_r(uint32_t funct7, uint32_t rs2, uint32_t rs1, uint32_t funct3, uint32_t rd, uint32_t opcode) noexcept {
        return ((funct7 & 0x7F) << 25) | ((rs2 & 0x1F) << 20) | ((rs1 & 0x1F) << 15) | ((funct3 & 0x7) << 12) | ((rd & 0x1F) << 7) | (opcode & 0x7F);
    }

    // I-type encoding
    static constexpr uint32_t encode_i(uint32_t imm12, uint32_t rs1, uint32_t funct3, uint32_t rd, uint32_t opcode) noexcept {
        return ((imm12 & 0xFFF) << 20) | ((rs1 & 0x1F) << 15) | ((funct3 & 0x7) << 12) | ((rd & 0x1F) << 7) | (opcode & 0x7F);
    }

    // S-type encoding
    static constexpr uint32_t encode_s(uint32_t imm12, uint32_t rs2, uint32_t rs1, uint32_t funct3, uint32_t opcode) noexcept {
        const uint32_t imm_11_5 = (imm12 >> 5) & 0x7F;
        const uint32_t imm_4_0  = imm12 & 0x1F;
        return (imm_11_5 << 25) | ((rs2 & 0x1F) << 20) | ((rs1 & 0x1F) << 15) | ((funct3 & 0x7) << 12) | (imm_4_0 << 7) | (opcode & 0x7F);
    }

    // B-type encoding
    static constexpr uint32_t encode_b(uint32_t imm13, uint32_t rs2, uint32_t rs1, uint32_t funct3, uint32_t opcode) noexcept {
        const uint32_t imm_12   = (imm13 >> 12) & 0x1;
        const uint32_t imm_10_5 = (imm13 >> 5)  & 0x3F;
        const uint32_t imm_4_1  = (imm13 >> 1)  & 0xF;
        const uint32_t imm_11   = (imm13 >> 11) & 0x1;
        return (imm_12 << 31) | (imm_10_5 << 25) | ((rs2 & 0x1F) << 20) | ((rs1 & 0x1F) << 15) | ((funct3 & 0x7) << 12) | (imm_4_1 << 8) | (imm_11 << 7) | (opcode & 0x7F);
    }

    // U-type encoding
    static constexpr uint32_t encode_u(uint32_t imm20, uint32_t rd, uint32_t opcode) noexcept {
        return ((imm20 & 0xFFFFF) << 12) | ((rd & 0x1F) << 7) | (opcode & 0x7F);
    }

    // J-type encoding
    static constexpr uint32_t encode_j(uint32_t imm21, uint32_t rd, uint32_t opcode) noexcept {
        const uint32_t imm_20    = (imm21 >> 20) & 0x1;
        const uint32_t imm_10_1  = (imm21 >> 1)  & 0x3FF;
        const uint32_t imm_11    = (imm21 >> 11) & 0x1;
        const uint32_t imm_19_12 = (imm21 >> 12) & 0xFF;
        return (imm_20 << 31) | (imm_10_1 << 21) | (imm_11 << 20) | (imm_19_12 << 12) | ((rd & 0x1F) << 7) | (opcode & 0x7F);
    }

    static bool encode_instruction(
        const MachineInstruction& inst,
        const MachineFunction& function,
        Section& text,
        ObjectFile& object,
        std::vector<RiscV64BranchPatch>& patches,
        const std::unordered_map<std::uint32_t, std::string>& symbol_table,
        std::string* out_diag) {

        const auto op = inst.opcode;
        const auto form = inst.form;

        uint32_t rd = 0, rs1 = 0, rs2 = 0;
        for (const auto& operand : inst.operands) {
            if (operand.kind == MachineOperandKind::Register) {
                if (operand.role == OperandRole::Def) {
                    rd = operand.reg.id & 0x1F;
                } else if (rs1 == 0) {
                    rs1 = operand.reg.id & 0x1F;
                } else {
                    rs2 = operand.reg.id & 0x1F;
                }
            }
        }

        switch (form) {
            case MachineForm::NoOp:
                // addi x0, x0, 0
                emit_u32(text, 0x00000013u);
                return true;

            case MachineForm::Return:
                // jalr x0, 0(ra) -> ret
                emit_u32(text, 0x00008067u);
                return true;

            case MachineForm::Move:
                // addi rd, rs1, 0  (mv alias)
                emit_u32(text, encode_i(0, rs1, 0, rd, 0x13));
                return true;

            case MachineForm::LoadImmediate: {
                int64_t imm = 0;
                for (const auto& operand : inst.operands) {
                    if (operand.kind == MachineOperandKind::Immediate) {
                        imm = operand.immediate;
                        break;
                    }
                }
                if (imm >= -2048 && imm <= 2047) {
                    // addi rd, x0, imm
                    emit_u32(text, encode_i(static_cast<uint32_t>(imm) & 0xFFF, 0, 0, rd, 0x13));
                } else {
                    // lui rd, hi20
                    const uint32_t hi20 = static_cast<uint32_t>((imm + 0x800) >> 12);
                    const uint32_t lo12 = static_cast<uint32_t>(imm) & 0xFFF;
                    emit_u32(text, encode_u(hi20, rd, 0x37));
                    if (lo12 != 0) {
                        emit_u32(text, encode_i(lo12, rd, 0, rd, 0x13));
                    }
                }
                return true;
            }

            case MachineForm::IntegerBinary32:
            case MachineForm::IntegerBinary64: {
                const bool is64 = (form == MachineForm::IntegerBinary64);
                const uint32_t opcode = is64 ? 0x33 : 0x3B; // OP vs OP-32
                const auto sem = inst.semantic_kind;

                if (sem == MachineSemanticKind::IntegerAdd || op == GM_IADD32 || op == GM_IADD64) {
                    emit_u32(text, encode_r(0x00, rs2, rs1, 0x0, rd, opcode)); // ADD
                    return true;
                }
                if (sem == MachineSemanticKind::IntegerSub || op == GM_ISUB32 || op == GM_ISUB64) {
                    emit_u32(text, encode_r(0x20, rs2, rs1, 0x0, rd, opcode)); // SUB
                    return true;
                }
                if (sem == MachineSemanticKind::IntegerMul || op == GM_IMUL32 || op == GM_IMUL64) {
                    emit_u32(text, encode_r(0x01, rs2, rs1, 0x0, rd, opcode)); // MUL
                    return true;
                }
                if (sem == MachineSemanticKind::IntegerDiv || op == GM_IDIV32 || op == GM_IDIV64) {
                    emit_u32(text, encode_r(0x01, rs2, rs1, 0x4, rd, opcode)); // DIV
                    return true;
                }
                if (sem == MachineSemanticKind::IntegerMod || op == GM_IMOD32 || op == GM_IMOD64) {
                    emit_u32(text, encode_r(0x01, rs2, rs1, 0x6, rd, opcode)); // REM
                    return true;
                }
                if (sem == MachineSemanticKind::IntegerAnd || op == GM_IAND32 || op == GM_IAND64) {
                    emit_u32(text, encode_r(0x00, rs2, rs1, 0x7, rd, 0x33)); // AND
                    return true;
                }
                if (sem == MachineSemanticKind::IntegerOr || op == GM_IOR32 || op == GM_IOR64) {
                    emit_u32(text, encode_r(0x00, rs2, rs1, 0x6, rd, 0x33)); // OR
                    return true;
                }
                if (sem == MachineSemanticKind::IntegerXor || op == GM_IXOR32 || op == GM_IXOR64) {
                    emit_u32(text, encode_r(0x00, rs2, rs1, 0x4, rd, 0x33)); // XOR
                    return true;
                }
                if (sem == MachineSemanticKind::IntegerShl || op == GM_ISHL32 || op == GM_ISHL64) {
                    emit_u32(text, encode_r(0x00, rs2, rs1, 0x1, rd, opcode)); // SLL
                    return true;
                }
                if (sem == MachineSemanticKind::IntegerShr || op == GM_ISHR32 || op == GM_ISHR64) {
                    emit_u32(text, encode_r(0x00, rs2, rs1, 0x5, rd, opcode)); // SRL
                    return true;
                }
                // Default ADD
                emit_u32(text, encode_r(0x00, rs2, rs1, 0x0, rd, opcode));
                return true;
            }

            case MachineForm::FloatBinary32:
            case MachineForm::FloatBinary64: {
                const bool is64 = (form == MachineForm::FloatBinary64);
                const uint32_t funct7_base = is64 ? 0x01u : 0x00u;
                const auto sem = inst.semantic_kind;

                if (sem == MachineSemanticKind::FloatSqrt || op == GM_FSQRT32 || op == GM_FSQRT64) {
                    const uint32_t f7 = is64 ? 0x2Du : 0x2Cu; // fsqrt.d vs fsqrt.s
                    emit_u32(text, encode_r(f7, 0, rs1, 0x7, rd, 0x53));
                    return true;
                }
                if (sem == MachineSemanticKind::FloatSub || op == GM_FSUB32 || op == GM_FSUB64) {
                    emit_u32(text, encode_r(funct7_base | 0x04u, rs2, rs1, 0x7, rd, 0x53)); // fsub
                    return true;
                }
                if (sem == MachineSemanticKind::FloatMul || op == GM_FMUL32 || op == GM_FMUL64) {
                    emit_u32(text, encode_r(funct7_base | 0x08u, rs2, rs1, 0x7, rd, 0x53)); // fmul
                    return true;
                }
                if (sem == MachineSemanticKind::FloatDiv || op == GM_FDIV32 || op == GM_FDIV64) {
                    emit_u32(text, encode_r(funct7_base | 0x0Cu, rs2, rs1, 0x7, rd, 0x53)); // fdiv
                    return true;
                }
                emit_u32(text, encode_r(funct7_base, rs2, rs1, 0x7, rd, 0x53)); // fadd
                return true;
            }

            case MachineForm::FloatTernaryFma32: {
                const bool is64 = (op == GM_FFMA64);
                const uint32_t fmt = is64 ? 1u : 0u;
                uint32_t rs3 = 0;
                if (inst.operands.size() >= 4 && inst.operands[3].kind == MachineOperandKind::Register) {
                    rs3 = inst.operands[3].reg.id & 0x1F;
                }
                emit_u32(text, ((rs3 & 0x1F) << 27) | (fmt << 25) | ((rs2 & 0x1F) << 20) | ((rs1 & 0x1F) << 15) | (0x7 << 12) | ((rd & 0x1F) << 7) | 0x43); // fmadd
                return true;
            }

            case MachineForm::Compare32: {
                const bool is_f64 = (op == GM_FCMP64 || op == GM_FCMP64_EQ || op == GM_FCMP64_NE ||
                                     op == GM_FCMP64_LT || op == GM_FCMP64_LE || op == GM_FCMP64_GT || op == GM_FCMP64_GE);
                const bool is_f32 = (op == GM_FCMP32 || op == GM_FCMP32_EQ || op == GM_FCMP32_NE ||
                                     op == GM_FCMP32_LT || op == GM_FCMP32_LE || op == GM_FCMP32_GT || op == GM_FCMP32_GE ||
                                     inst.semantic_kind == MachineSemanticKind::FloatCmp);

                if (is_f64) {
                    const auto pred = inst.comparison_predicate();
                    if (pred == ComparisonPredicate::Equal) {
                        emit_u32(text, encode_r(0x51, rs2, rs1, 0x2, rd, 0x53)); // feq.d
                    } else if (pred == ComparisonPredicate::SignedLessEqual) {
                        emit_u32(text, encode_r(0x51, rs2, rs1, 0x0, rd, 0x53)); // fle.d
                    } else {
                        emit_u32(text, encode_r(0x51, rs2, rs1, 0x1, rd, 0x53)); // flt.d
                    }
                    return true;
                }
                if (is_f32) {
                    const auto pred = inst.comparison_predicate();
                    if (pred == ComparisonPredicate::Equal) {
                        emit_u32(text, encode_r(0x50, rs2, rs1, 0x2, rd, 0x53)); // feq.s
                    } else if (pred == ComparisonPredicate::SignedLessEqual) {
                        emit_u32(text, encode_r(0x50, rs2, rs1, 0x0, rd, 0x53)); // fle.s
                    } else {
                        emit_u32(text, encode_r(0x50, rs2, rs1, 0x1, rd, 0x53)); // flt.s
                    }
                    return true;
                }

                switch (inst.comparison_predicate()) {
                    case ComparisonPredicate::Equal:
                        // xor rd, rs1, rs2; sltiu rd, rd, 1 (seqz)
                        emit_u32(text, encode_r(0x00, rs2, rs1, 0x4, rd, 0x33)); // xor
                        emit_u32(text, encode_i(1, rd, 0x3, rd, 0x13));           // sltiu rd, rd, 1
                        return true;
                    case ComparisonPredicate::NotEqual:
                        // xor rd, rs1, rs2; sltu rd, x0, rd (snez)
                        emit_u32(text, encode_r(0x00, rs2, rs1, 0x4, rd, 0x33)); // xor
                        emit_u32(text, encode_r(0x00, rd, 0, 0x3, rd, 0x33));    // sltu rd, x0, rd
                        return true;
                    case ComparisonPredicate::SignedGreaterThan:
                        // slt rd, rs2, rs1
                        emit_u32(text, encode_r(0x00, rs1, rs2, 0x2, rd, 0x33));
                        return true;
                    case ComparisonPredicate::SignedLessEqual:
                        // slt rd, rs2, rs1; xori rd, rd, 1
                        emit_u32(text, encode_r(0x00, rs1, rs2, 0x2, rd, 0x33));
                        emit_u32(text, encode_i(1, rd, 0x4, rd, 0x13));
                        return true;
                    case ComparisonPredicate::UnsignedLessThan:
                        // sltu rd, rs1, rs2
                        emit_u32(text, encode_r(0x00, rs2, rs1, 0x3, rd, 0x33));
                        return true;
                    case ComparisonPredicate::UnsignedGreaterEqual:
                        // sltu rd, rs1, rs2; xori rd, rd, 1
                        emit_u32(text, encode_r(0x00, rs2, rs1, 0x3, rd, 0x33));
                        emit_u32(text, encode_i(1, rd, 0x4, rd, 0x13));
                        return true;
                    default:
                        // slt rd, rs1, rs2
                        emit_u32(text, encode_r(0x00, rs2, rs1, 0x2, rd, 0x33));
                        return true;
                }
            }

            case MachineForm::LoadStack: {
                uint32_t base = (rs1 != 0) ? rs1 : 2; // sp = x2
                uint32_t off12 = 0;
                for (const auto& operand : inst.operands) {
                    if (operand.kind == MachineOperandKind::Immediate) {
                        off12 = static_cast<uint32_t>(operand.immediate) & 0xFFFu;
                    }
                }
                const bool is_fp = (inst.semantic_kind == MachineSemanticKind::FloatLoad ||
                                    op == GM_FLOAD32 || op == GM_FLOAD64);
                if (is_fp && op == GM_FLOAD32) {
                    // flw rd, off12(base) -> opcode=0x07 funct3=0x2
                    emit_u32(text, encode_i(off12, base, 0x2, rd, 0x07));
                } else if (is_fp) {
                    // fld rd, off12(base) -> opcode=0x07 funct3=0x3
                    emit_u32(text, encode_i(off12, base, 0x3, rd, 0x07));
                } else {
                    // ld rd, off12(base) -> funct3=011 opcode=0x03
                    emit_u32(text, encode_i(off12, base, 0x3, rd, 0x03));
                }
                return true;
            }

            case MachineForm::StoreStack: {
                uint32_t src = (rd != 0) ? rd : rs1;
                uint32_t base = (rs1 != src) ? rs1 : 2; // sp
                uint32_t off12 = 0;
                for (const auto& operand : inst.operands) {
                    if (operand.kind == MachineOperandKind::Immediate) {
                        off12 = static_cast<uint32_t>(operand.immediate) & 0xFFFu;
                    }
                }
                const bool is_fp = (inst.semantic_kind == MachineSemanticKind::FloatStore ||
                                    op == GM_FSTORE32 || op == GM_FSTORE64);
                if (is_fp && op == GM_FSTORE32) {
                    // fsw src, off12(base) -> opcode=0x27 funct3=0x2
                    emit_u32(text, encode_s(off12, src, base, 0x2, 0x27));
                } else if (is_fp) {
                    // fsd src, off12(base) -> opcode=0x27 funct3=0x3
                    emit_u32(text, encode_s(off12, src, base, 0x3, 0x27));
                } else {
                    // sd src, off12(base) -> funct3=011 opcode=0x23
                    emit_u32(text, encode_s(off12, src, base, 0x3, 0x23));
                }
                return true;
            }

            case MachineForm::Goto: {
                if (!inst.operands.empty() && inst.operands[0].kind == MachineOperandKind::BasicBlock) {
                    patches.push_back({text.size(), inst.operands[0].block, true, 0, 0, 0});
                    emit_u32(text, encode_j(0, 0, 0x6F)); // jal x0, 0 placeholder
                    return true;
                }
                emit_u32(text, encode_j(0, 0, 0x6F));
                return true;
            }

            case MachineForm::BranchCond: {
                if (inst.operands.size() >= 2 && inst.operands[1].kind == MachineOperandKind::BasicBlock) {
                    const uint32_t reg = inst.operands[0].kind == MachineOperandKind::Register ? (inst.operands[0].reg.id & 0x1F) : 0u;
                    // bne reg, x0, target
                    patches.push_back({text.size(), inst.operands[1].block, false, 0x1, reg, 0});
                    emit_u32(text, encode_b(0, 0, reg, 0x1, 0x63));
                    return true;
                }
                emit_u32(text, 0x00000013u);
                return true;
            }

            case MachineForm::BranchCompare: {
                if (inst.operands.size() >= 3 && inst.operands[2].kind == MachineOperandKind::BasicBlock) {
                    uint32_t funct3 = 0x0; // BEQ
                    switch (inst.comparison_predicate()) {
                        case ComparisonPredicate::Equal:        funct3 = 0x0; break; // BEQ
                        case ComparisonPredicate::NotEqual:     funct3 = 0x1; break; // BNE
                        case ComparisonPredicate::SignedLessThan:   funct3 = 0x4; break; // BLT
                        case ComparisonPredicate::SignedGreaterEqual: funct3 = 0x5; break; // BGE
                        case ComparisonPredicate::UnsignedLessThan: funct3 = 0x6; break; // BLTU
                        case ComparisonPredicate::UnsignedGreaterEqual: funct3 = 0x7; break; // BGEU
                        default: funct3 = 0x0; break;
                    }
                    patches.push_back({text.size(), inst.operands[2].block, false, funct3, rs1, rs2});
                    emit_u32(text, encode_b(0, rs2, rs1, funct3, 0x63));
                    return true;
                }
                emit_u32(text, 0x00000013u);
                return true;
            }

            case MachineForm::Call: {
                const uint64_t reloc_offset = text.size();
                // auipc ra, 0; jalr ra, 0(ra) -> standard call pair
                emit_u32(text, encode_u(0, 1, 0x17));       // auipc ra, 0
                emit_u32(text, encode_i(0, 1, 0, 1, 0x67)); // jalr ra, 0(ra)

                std::string target_name;
                for (const auto& operand : inst.operands) {
                    if (operand.kind == MachineOperandKind::Symbol) {
                        auto it = symbol_table.find(operand.symbol);
                        if (it != symbol_table.end()) {
                            target_name = it->second;
                            break;
                        }
                    }
                    if (operand.kind == MachineOperandKind::Immediate) {
                        auto it = symbol_table.find(static_cast<uint32_t>(operand.immediate));
                        if (it != symbol_table.end()) {
                            target_name = it->second;
                            break;
                        }
                    }
                }
                if (target_name.empty()) {
                    if (const char* sym = dgtl::mir::runtime_contract::runtime_symbol_for_semantic_kind(inst.semantic_kind)) {
                        target_name = sym;
                    } else if (const char* sym = dgtl::mir::runtime_contract::runtime_symbol_for_opcode(op)) {
                        target_name = sym;
                    }
                }
                if (!target_name.empty()) {
                    object.add_relocation(
                        reloc_offset,
                        target_name,
                        0,
                        0x0012, // R_RISCV_CALL
                        dgtl::object::RelocationKind::PCRel32,
                        0
                    );
                }
                return true;
            }

            default: {
                const char* sym = dgtl::mir::runtime_contract::runtime_symbol_for_semantic_kind(inst.semantic_kind);
                if (!sym) sym = dgtl::mir::runtime_contract::runtime_symbol_for_opcode(op);
                if (sym) {
                    const uint64_t reloc_offset = text.size();
                    emit_u32(text, encode_u(0, 1, 0x17));
                    emit_u32(text, encode_i(0, 1, 0, 1, 0x67));
                    object.add_relocation(
                        reloc_offset,
                        sym,
                        0,
                        0x0012,
                        dgtl::object::RelocationKind::PCRel32,
                        0
                    );
                    return true;
                }
                emit_u32(text, 0x00000013u); // NOP
                return true;
            }
        }
    }

    static bool encode_function(
        const MachineFunction& function,
        ObjectFile& object,
        std::string_view symbol_name,
        const std::unordered_map<std::uint32_t, std::string>& symbol_table = {},
        std::string* out_diag = nullptr) {

        Section& text = object.get_text_section();
        const std::uint64_t func_start = text.size();

        std::unordered_map<BlockId, std::size_t> block_offsets;
        std::vector<RiscV64BranchPatch> patches;

        for (std::size_t b_idx = 0; b_idx < function.blocks().size(); ++b_idx) {
            const MachineBasicBlock* bb = function.block(static_cast<BlockId>(b_idx));
            if (!bb) continue;

            block_offsets[bb->id] = text.size();

            for (const InstrId inst_id : bb->instructions) {
                const MachineInstruction* inst = function.instruction(inst_id);
                if (!inst || inst->parent_block == INVALID_BLOCK_ID) continue;

                if (!encode_instruction(*inst, function, text, object, patches, symbol_table, out_diag)) {
                    if (out_diag && out_diag->empty()) {
                        *out_diag = "RISC-V 64 encoder failed at instruction ID " + std::to_string(inst->id);
                    }
                    return false;
                }
            }
        }

        // Branch and jump fixup pass
        for (const auto& patch : patches) {
            auto it = block_offsets.find(patch.target_block);
            if (it == block_offsets.end()) {
                if (out_diag) {
                    *out_diag = "RISC-V 64 Branch patch failed: target block " + std::to_string(patch.target_block) + " not found";
                }
                return false;
            }
            const std::size_t target_offset = it->second;
            const std::int64_t delta_bytes = static_cast<std::int64_t>(target_offset) - static_cast<std::int64_t>(patch.patch_offset);

            uint32_t insn = 0;
            if (patch.is_jal) {
                // JAL x0, imm21
                if (delta_bytes < -(1 << 20) || delta_bytes >= (1 << 20)) {
                    if (out_diag) *out_diag = "RISC-V 64 JAL displacement out of range";
                    return false;
                }
                insn = encode_j(static_cast<uint32_t>(delta_bytes), 0, 0x6F);
            } else {
                // B-type imm13
                if (delta_bytes < -(1 << 12) || delta_bytes >= (1 << 12)) {
                    if (out_diag) *out_diag = "RISC-V 64 Branch displacement out of range";
                    return false;
                }
                insn = encode_b(static_cast<uint32_t>(delta_bytes), patch.rs2, patch.rs1, patch.funct3, 0x63);
            }

            std::memcpy(text.bytes.data() + patch.patch_offset, &insn, 4);
        }

        const std::uint64_t func_size = text.size() - func_start;
        object.add_function_symbol(std::string(symbol_name), func_start, func_size, true);
        return true;
    }
};

} // namespace dgtl::isa::riscv64
