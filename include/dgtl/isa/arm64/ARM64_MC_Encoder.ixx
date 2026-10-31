/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Component:   ARM64 (AArch64) Standalone Machine Code Binary Encoder
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

export module dgtl.isa.arm64.mc_encoder;

import dgtl.mir.machine_ir;
import dgtl.mir.runtime_contract;
import dgtl.object.file;
import dgtl.object.section;
import dgtl.object.symbol;
import dgtl.object.relocation;
import dgtl.isa.arm64.arm64_instruction_info;
import dgtl.isa.arm64.arm64_reg_map;

export namespace dgtl::isa::arm64 {

using namespace dgtl::machine;
using namespace dgtl::object;

struct ARM64BranchPatch {
    std::size_t patch_offset = 0;
    BlockId target_block = INVALID_BLOCK_ID;
    bool is_cond = false;
    uint32_t cond = 0;
    uint32_t cond_reg = 0xFFFFFFFFu;
};

class ARM64MCEncoder {
public:
    static void emit_u32(Section& sec, uint32_t val) {
        sec.bytes.push_back(static_cast<uint8_t>(val & 0xFF));
        sec.bytes.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
        sec.bytes.push_back(static_cast<uint8_t>((val >> 16) & 0xFF));
        sec.bytes.push_back(static_cast<uint8_t>((val >> 24) & 0xFF));
    }

    static uint32_t bcond_code(ComparisonPredicate pred) noexcept {
        switch (pred) {
            case ComparisonPredicate::Equal:        return 0x0; // EQ
            case ComparisonPredicate::NotEqual:     return 0x1; // NE
            case ComparisonPredicate::SignedGreaterThan: return 0xC; // GT
            case ComparisonPredicate::SignedGreaterEqual: return 0xA; // GE
            case ComparisonPredicate::SignedLessThan:   return 0xB; // LT
            case ComparisonPredicate::SignedLessEqual: return 0xD; // LE
            case ComparisonPredicate::UnsignedGreaterThan: return 0x8; // HI
            case ComparisonPredicate::UnsignedGreaterEqual: return 0x2; // CS/HS
            case ComparisonPredicate::UnsignedLessThan: return 0x3; // CC/LO
            case ComparisonPredicate::UnsignedLessEqual: return 0x9; // LS
            default: return 0xE; // AL
        }
    }

    static bool encode_instruction(
        const MachineInstruction& inst,
        const MachineFunction& function,
        Section& text,
        ObjectFile& object,
        std::vector<ARM64BranchPatch>& patches,
        const std::unordered_map<std::uint32_t, std::string>& symbol_table,
        std::string* out_diag) {

        const auto op = inst.opcode;
        const auto form = inst.form;

        uint32_t rd = 0, rn = 0, rm = 0;
        for (const auto& operand : inst.operands) {
            if (operand.kind == MachineOperandKind::Register) {
                if (operand.role == OperandRole::Def) {
                    rd = operand.reg.id & 0x1F;
                } else if (rn == 0) {
                    rn = operand.reg.id & 0x1F;
                } else {
                    rm = operand.reg.id & 0x1F;
                }
            }
        }

        switch (form) {
            case MachineForm::NoOp:
                emit_u32(text, 0xD503201Fu); // NOP
                return true;

            case MachineForm::Return:
                emit_u32(text, 0xD65F03C0u); // RET x30
                return true;

            case MachineForm::Move:
                // ORR Wd, WZR, Wn  (mov alias: 0x2A0003E0 | (rn << 16) | rd)
                emit_u32(text, 0x2A0003E0u | (rn << 16) | rd);
                return true;

            case MachineForm::LoadImmediate: {
                uint32_t imm16 = 0;
                for (const auto& operand : inst.operands) {
                    if (operand.kind == MachineOperandKind::Immediate) {
                        imm16 = static_cast<uint32_t>(operand.immediate) & 0xFFFFu;
                        break;
                    }
                }
                // MOVZ Xd, #imm16
                emit_u32(text, 0xD2800000u | (imm16 << 5) | rd);
                return true;
            }

            case MachineForm::IntegerBinary32:
            case MachineForm::IntegerBinary64: {
                const bool is64 = (form == MachineForm::IntegerBinary64);
                const uint32_t sf = is64 ? (1u << 31) : 0u;
                const auto sem = inst.semantic_kind;

                if (sem == MachineSemanticKind::IntegerAdd || op == GM_IADD32 || op == GM_IADD64) {
                    emit_u32(text, sf | 0x0B000000u | (rm << 16) | (rn << 5) | rd); // ADD
                    return true;
                }
                if (sem == MachineSemanticKind::IntegerSub || op == GM_ISUB32 || op == GM_ISUB64) {
                    emit_u32(text, sf | 0x4B000000u | (rm << 16) | (rn << 5) | rd); // SUB
                    return true;
                }
                if (sem == MachineSemanticKind::IntegerMul || op == GM_IMUL32 || op == GM_IMUL64) {
                    emit_u32(text, sf | 0x1B007C00u | (rm << 16) | (rn << 5) | rd); // MADD (Ra = XZR)
                    return true;
                }
                if (sem == MachineSemanticKind::IntegerAnd || op == GM_IAND32 || op == GM_IAND64) {
                    emit_u32(text, sf | 0x0A000000u | (rm << 16) | (rn << 5) | rd); // AND
                    return true;
                }
                if (sem == MachineSemanticKind::IntegerOr || op == GM_IOR32 || op == GM_IOR64) {
                    emit_u32(text, sf | 0x2A000000u | (rm << 16) | (rn << 5) | rd); // ORR
                    return true;
                }
                if (sem == MachineSemanticKind::IntegerXor || op == GM_IXOR32 || op == GM_IXOR64) {
                    emit_u32(text, sf | 0x4A000000u | (rm << 16) | (rn << 5) | rd); // EOR
                    return true;
                }
                if (sem == MachineSemanticKind::IntegerDiv || op == GM_IDIV32 || op == GM_IDIV64) {
                    emit_u32(text, sf | 0x1AC00C00u | (rm << 16) | (rn << 5) | rd); // SDIV
                    return true;
                }
                if (sem == MachineSemanticKind::IntegerMod || op == GM_IMOD32 || op == GM_IMOD64) {
                    emit_u32(text, sf | 0x1AC00C00u | (rm << 16) | (rn << 5) | 16u); // SDIV X16, Xn, Xm
                    emit_u32(text, sf | 0x1B008000u | (rm << 16) | (rn << 10) | (16u << 5) | rd); // MSUB Xd, X16, Xm, Xn
                    return true;
                }
                if (sem == MachineSemanticKind::IntegerShl || op == GM_ISHL32 || op == GM_ISHL64) {
                    emit_u32(text, sf | 0x1AC02000u | (rm << 16) | (rn << 5) | rd); // LSLV
                    return true;
                }
                if (sem == MachineSemanticKind::IntegerShr || op == GM_ISHR32 || op == GM_ISHR64) {
                    emit_u32(text, sf | 0x1AC02400u | (rm << 16) | (rn << 5) | rd); // LSRV
                    return true;
                }
                // Fallback default ADD
                emit_u32(text, sf | 0x0B000000u | (rm << 16) | (rn << 5) | rd);
                return true;
            }

            case MachineForm::FloatBinary32:
            case MachineForm::FloatBinary64: {
                const bool is64 = (form == MachineForm::FloatBinary64);
                const uint32_t ftype = is64 ? (1u << 22) : 0u; // 0 for S, 1 for D
                const auto sem = inst.semantic_kind;

                if (sem == MachineSemanticKind::FloatSqrt || op == GM_FSQRT32 || op == GM_FSQRT64) {
                    emit_u32(text, 0x1E21C000u | ftype | (rn << 5) | rd); // FSQRT
                    return true;
                }
                if (sem == MachineSemanticKind::FloatSub || op == GM_FSUB32 || op == GM_FSUB64) {
                    emit_u32(text, 0x1E203800u | ftype | (rm << 16) | (rn << 5) | rd); // FSUB
                    return true;
                }
                if (sem == MachineSemanticKind::FloatMul || op == GM_FMUL32 || op == GM_FMUL64) {
                    emit_u32(text, 0x1E200800u | ftype | (rm << 16) | (rn << 5) | rd); // FMUL
                    return true;
                }
                if (sem == MachineSemanticKind::FloatDiv || op == GM_FDIV32 || op == GM_FDIV64) {
                    emit_u32(text, 0x1E201800u | ftype | (rm << 16) | (rn << 5) | rd); // FDIV
                    return true;
                }
                emit_u32(text, 0x1E202800u | ftype | (rm << 16) | (rn << 5) | rd); // FADD
                return true;
            }

            case MachineForm::FloatTernaryFma32: {
                const bool is64 = (op == GM_FFMA64);
                const uint32_t ftype = is64 ? (1u << 22) : 0u;
                uint32_t ra = 0;
                if (inst.operands.size() >= 4 && inst.operands[3].kind == MachineOperandKind::Register) {
                    ra = inst.operands[3].reg.id & 0x1F;
                }
                emit_u32(text, 0x1F000000u | ftype | (rm << 16) | (ra << 10) | (rn << 5) | rd); // FMADD
                return true;
            }

            case MachineForm::Compare32: {
                const bool is_f64 = (op == GM_FCMP64 || op == GM_FCMP64_EQ || op == GM_FCMP64_NE ||
                                     op == GM_FCMP64_LT || op == GM_FCMP64_LE || op == GM_FCMP64_GT || op == GM_FCMP64_GE);
                const bool is_f32 = (op == GM_FCMP32 || op == GM_FCMP32_EQ || op == GM_FCMP32_NE ||
                                     op == GM_FCMP32_LT || op == GM_FCMP32_LE || op == GM_FCMP32_GT || op == GM_FCMP32_GE ||
                                     inst.semantic_kind == MachineSemanticKind::FloatCmp);
                const bool is_i64 = (op == GM_ICMP64 || op == GM_ICMP64_EQ || op == GM_ICMP64_NE ||
                                     op == GM_ICMP64_LT || op == GM_ICMP64_LE || op == GM_ICMP64_GT || op == GM_ICMP64_GE);

                if (is_f64) {
                    emit_u32(text, 0x1E602008u | (rm << 16) | (rn << 5)); // FCMP Dn, Dm
                } else if (is_f32) {
                    emit_u32(text, 0x1E202008u | (rm << 16) | (rn << 5)); // FCMP Sn, Sm
                } else if (is_i64) {
                    emit_u32(text, 0xEB00001Fu | (rm << 16) | (rn << 5)); // SUBS XZR, Xn, Xm (CMP Xn, Xm)
                } else {
                    emit_u32(text, 0x6B00001Fu | (rm << 16) | (rn << 5)); // SUBS WZR, Wn, Wm (CMP Wn, Wm)
                }

                if (rd != 0) {
                    const uint32_t cond = bcond_code(inst.comparison_predicate());
                    const uint32_t inv_cond = cond ^ 1u;
                    emit_u32(text, 0x1A9F07E0u | (inv_cond << 12) | rd); // CSET Wd, cond
                }
                return true;
            }

            case MachineForm::LoadStack: {
                uint32_t base = (rn != 0) ? rn : 31; // SP / X31
                uint32_t off12 = 0;
                for (const auto& operand : inst.operands) {
                    if (operand.kind == MachineOperandKind::Immediate) {
                        off12 = (static_cast<uint32_t>(operand.immediate) & 0xFFFu) >> 3;
                    }
                }
                const bool is_fp = (inst.semantic_kind == MachineSemanticKind::FloatLoad ||
                                    op == GM_FLOAD32 || op == GM_FLOAD64);
                if (is_fp && op == GM_FLOAD32) {
                    // LDR St, [Xn, #offset] (32-bit float: 0xBD400000 | (off12 << 10) | (base << 5) | rd)
                    const uint32_t off_w = (off12 << 1);
                    emit_u32(text, 0xBD400000u | (off_w << 10) | (base << 5) | rd);
                } else if (is_fp) {
                    // LDR Dt, [Xn, #offset] (64-bit float: 0xFD400000 | (off12 << 10) | (base << 5) | rd)
                    emit_u32(text, 0xFD400000u | (off12 << 10) | (base << 5) | rd);
                } else {
                    // LDR Xd, [Xn, #offset] (64-bit int: 0xF9400000 | (off12 << 10) | (base << 5) | rd)
                    emit_u32(text, 0xF9400000u | (off12 << 10) | (base << 5) | rd);
                }
                return true;
            }

            case MachineForm::StoreStack: {
                uint32_t val = (rd != 0) ? rd : rn;
                uint32_t base = (rn != val) ? rn : 31; // SP
                uint32_t off12 = 0;
                for (const auto& operand : inst.operands) {
                    if (operand.kind == MachineOperandKind::Immediate) {
                        off12 = (static_cast<uint32_t>(operand.immediate) & 0xFFFu) >> 3;
                    }
                }
                const bool is_fp = (inst.semantic_kind == MachineSemanticKind::FloatStore ||
                                    op == GM_FSTORE32 || op == GM_FSTORE64);
                if (is_fp && op == GM_FSTORE32) {
                    // STR St, [Xn, #offset] (32-bit float: 0xBD000000 | (off12 << 10) | (base << 5) | val)
                    const uint32_t off_w = (off12 << 1);
                    emit_u32(text, 0xBD000000u | (off_w << 10) | (base << 5) | val);
                } else if (is_fp) {
                    // STR Dt, [Xn, #offset] (64-bit float: 0xFD000000 | (off12 << 10) | (base << 5) | val)
                    emit_u32(text, 0xFD000000u | (off12 << 10) | (base << 5) | val);
                } else {
                    // STR Xval, [Xn, #offset] (64-bit int: 0xF9000000 | (off12 << 10) | (base << 5) | val)
                    emit_u32(text, 0xF9000000u | (off12 << 10) | (base << 5) | val);
                }
                return true;
            }

            case MachineForm::Goto: {
                if (!inst.operands.empty() && inst.operands[0].kind == MachineOperandKind::BasicBlock) {
                    patches.push_back({text.size(), inst.operands[0].block, false, 0, 0xFFFFFFFFu});
                    emit_u32(text, 0x14000000u); // B #0 placeholder
                    return true;
                }
                emit_u32(text, 0x14000000u);
                return true;
            }

            case MachineForm::BranchCond: {
                if (inst.operands.size() >= 2 && inst.operands[1].kind == MachineOperandKind::BasicBlock) {
                    const uint32_t reg = inst.operands[0].kind == MachineOperandKind::Register ? (inst.operands[0].reg.id & 0x1F) : 0u;
                    patches.push_back({text.size(), inst.operands[1].block, true, 0, reg});
                    emit_u32(text, 0xB5000000u | reg); // CBNZ Xreg, #0
                    return true;
                }
                emit_u32(text, 0xD503201Fu);
                return true;
            }

            case MachineForm::BranchCompare: {
                if (inst.operands.size() >= 3 && inst.operands[2].kind == MachineOperandKind::BasicBlock) {
                    // 1. CMP Wn, Wm
                    emit_u32(text, 0x6B00001Fu | (rm << 16) | (rn << 5));
                    // 2. B.<cond> #0 placeholder
                    const uint32_t cond = bcond_code(inst.comparison_predicate());
                    patches.push_back({text.size(), inst.operands[2].block, true, cond, 0xFFFFFFFFu});
                    emit_u32(text, 0x54000000u | cond);
                    return true;
                }
                emit_u32(text, 0xD503201Fu);
                return true;
            }

            case MachineForm::Call: {
                const uint64_t reloc_offset = text.size();
                emit_u32(text, 0x94000000u); // BL #0 placeholder

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
                        0x011B, // R_AARCH64_CALL26
                        dgtl::object::RelocationKind::Call26,
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
                    emit_u32(text, 0x94000000u); // BL #0
                    object.add_relocation(
                        reloc_offset,
                        sym,
                        0,
                        0x011B,
                        dgtl::object::RelocationKind::Call26,
                        0
                    );
                    return true;
                }
                emit_u32(text, 0xD503201Fu); // NOP
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
        std::vector<ARM64BranchPatch> patches;

        for (std::size_t b_idx = 0; b_idx < function.blocks().size(); ++b_idx) {
            const MachineBasicBlock* bb = function.block(static_cast<BlockId>(b_idx));
            if (!bb) continue;

            block_offsets[bb->id] = text.size();

            for (const InstrId inst_id : bb->instructions) {
                const MachineInstruction* inst = function.instruction(inst_id);
                if (!inst || inst->parent_block == INVALID_BLOCK_ID) continue;

                if (!encode_instruction(*inst, function, text, object, patches, symbol_table, out_diag)) {
                    if (out_diag && out_diag->empty()) {
                        *out_diag = "ARM64 encoder failed at instruction ID " + std::to_string(inst->id);
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
                    *out_diag = "ARM64 Branch patch failed: target block " + std::to_string(patch.target_block) + " not found";
                }
                return false;
            }
            const std::size_t target_offset = it->second;
            const std::int64_t delta_bytes = static_cast<std::int64_t>(target_offset) - static_cast<std::int64_t>(patch.patch_offset);
            const std::int64_t delta_insns = delta_bytes / 4;

            uint32_t insn = 0;
            std::memcpy(&insn, text.bytes.data() + patch.patch_offset, 4);

            if (!patch.is_cond) {
                // B imm26
                if (delta_insns < -(1 << 25) || delta_insns >= (1 << 25)) {
                    if (out_diag) *out_diag = "ARM64 B displacement out of range";
                    return false;
                }
                insn = 0x14000000u | (static_cast<uint32_t>(delta_insns) & 0x03FFFFFFu);
            } else if (patch.cond_reg != 0xFFFFFFFFu) {
                // CBNZ Xn, imm19
                if (delta_insns < -(1 << 18) || delta_insns >= (1 << 18)) {
                    if (out_diag) *out_diag = "ARM64 CBNZ displacement out of range";
                    return false;
                }
                insn = 0xB5000000u | ((static_cast<uint32_t>(delta_insns) & 0x7FFFFu) << 5) | (patch.cond_reg & 0x1Fu);
            } else {
                // B.<cond> imm19
                if (delta_insns < -(1 << 18) || delta_insns >= (1 << 18)) {
                    if (out_diag) *out_diag = "ARM64 B.cond displacement out of range";
                    return false;
                }
                insn = 0x54000000u | ((static_cast<uint32_t>(delta_insns) & 0x7FFFFu) << 5) | (patch.cond & 0x0Fu);
            }

            std::memcpy(text.bytes.data() + patch.patch_offset, &insn, 4);
        }

        const std::uint64_t func_size = text.size() - func_start;
        object.add_function_symbol(std::string(symbol_name), func_start, func_size, true);
        return true;
    }
};

} // namespace dgtl::isa::arm64
