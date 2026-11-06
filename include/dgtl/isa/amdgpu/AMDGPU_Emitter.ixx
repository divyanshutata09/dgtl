/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Component:   ROCm / AMDGPU High-Performance Code Emitter
 * Standards:   C++26 / Clang 18+ / MSVC 19.40+ / GCC 14+
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

module;

#include <cstdlib>
#include <string>
#include <sstream>
#include <vector>
#include <cstdint>

export module dgtl.isa.amdgpu.emitter;

import dgtl.mir.machine_ir;
import dgtl.mir.runtime_contract;

export namespace dgtl::isa::amdgpu {

class ROCmEmitter final : public dgtl::machine::MachineEmitter,
                          public dgtl::machine::MachineCodeEmitter {
public:
    [[nodiscard]] dgtl::machine::EmissionKind kind() const noexcept override {
        return dgtl::machine::EmissionKind::Assembly;
    }

    void emit_instruction(const dgtl::machine::MachineInstruction& inst,
                          std::vector<std::uint8_t>& out) override {
        std::ostringstream asm_out;
        emit_instruction_text(inst, asm_out);
        std::string str = asm_out.str();
        out.insert(out.end(), str.begin(), str.end());
    }

    static void emit_instruction_text(const dgtl::machine::MachineInstruction& inst_ref, std::ostream& asm_out) {
        const auto* inst = &inst_ref;
        uint32_t vd = 0, vs1 = 0, vs2 = 0, vs3 = 0;
        int64_t imm = 0;
        bool is_dst_fp = false;
        bool is_src_fp = false;
        for (const auto& op : inst->operands) {
            if (op.kind == dgtl::machine::MachineOperandKind::Register) {
                if (op.role == dgtl::machine::OperandRole::Def) {
                    vd = op.reg.id & 0x3F;
                    is_dst_fp = (op.reg.reg_class == dgtl::machine::RegisterClassKind::FloatingPoint);
                } else if (vs1 == 0) {
                    vs1 = op.reg.id & 0x3F;
                    is_src_fp = (op.reg.reg_class == dgtl::machine::RegisterClassKind::FloatingPoint);
                } else if (vs2 == 0) {
                    vs2 = op.reg.id & 0x3F;
                } else {
                    vs3 = op.reg.id & 0x3F;
                }
            } else if (op.kind == dgtl::machine::MachineOperandKind::Immediate) {
                imm = op.immediate;
            }
        }

        switch (inst->form) {
                    case dgtl::machine::MachineForm::NoOp:
                        asm_out << "    s_nop 0\n";
                        break;
                    case dgtl::machine::MachineForm::Return:
                        asm_out << "    s_endpgm\n";
                        break;
                    case dgtl::machine::MachineForm::Move:
                        if (is_dst_fp && !is_src_fp) {
                            asm_out << "    v_cvt_f64_i32 v[" << vd << ":" << (vd + 1) << "], v" << vs1 << "\n";
                        } else if (!is_dst_fp && is_src_fp) {
                            asm_out << "    v_cvt_i32_f64 v" << vd << ", v[" << vs1 << ":" << (vs1 + 1) << "]\n";
                        } else {
                            asm_out << "    v_mov_b32 v" << vd << ", v" << vs1 << "\n";
                        }
                        break;
                    case dgtl::machine::MachineForm::LoadImmediate:
                        asm_out << "    v_mov_b32 v" << vd << ", " << imm << "\n";
                        break;
                    case dgtl::machine::MachineForm::IntegerBinary32: {
                        const char* op_str = "v_add_u32";
                        switch (inst->semantic_kind) {
                            case dgtl::machine::MachineSemanticKind::IntegerAdd: op_str = "v_add_u32"; break;
                            case dgtl::machine::MachineSemanticKind::IntegerSub: op_str = "v_sub_u32"; break;
                            case dgtl::machine::MachineSemanticKind::IntegerMul: op_str = "v_mul_lo_u32"; break;
                            case dgtl::machine::MachineSemanticKind::IntegerAnd: op_str = "v_and_b32"; break;
                            case dgtl::machine::MachineSemanticKind::IntegerOr:  op_str = "v_or_b32"; break;
                            case dgtl::machine::MachineSemanticKind::IntegerXor: op_str = "v_xor_b32"; break;
                            case dgtl::machine::MachineSemanticKind::IntegerShl:  op_str = "v_lshlrev_b32"; break;
                            case dgtl::machine::MachineSemanticKind::IntegerShr: op_str = "v_ashrrev_i32"; break;
                            default: break;
                        }
                        asm_out << "    " << op_str << " v" << vd << ", v" << vs1 << ", v" << vs2 << "\n";
                        break;
                    }
                    case dgtl::machine::MachineForm::IntegerBinary64: {
                        const char* op_str = "v_add_u64";
                        switch (inst->semantic_kind) {
                            case dgtl::machine::MachineSemanticKind::IntegerAdd: op_str = "v_add_u64"; break;
                            case dgtl::machine::MachineSemanticKind::IntegerSub: op_str = "v_sub_u64"; break;
                            case dgtl::machine::MachineSemanticKind::IntegerMul: op_str = "v_mul_lo_u64"; break;
                            case dgtl::machine::MachineSemanticKind::IntegerAnd: op_str = "v_and_b64"; break;
                            case dgtl::machine::MachineSemanticKind::IntegerOr:  op_str = "v_or_b64"; break;
                            case dgtl::machine::MachineSemanticKind::IntegerXor: op_str = "v_xor_b64"; break;
                            case dgtl::machine::MachineSemanticKind::IntegerShl: op_str = "v_lshlrev_b64"; break;
                            case dgtl::machine::MachineSemanticKind::IntegerShr: op_str = "v_ashrrev_i64"; break;
                            default: break;
                        }
                        asm_out << "    " << op_str << " v" << vd << ", v" << vs1 << ", v" << vs2 << "\n";
                        break;
                    }
                    case dgtl::machine::MachineForm::FloatBinary32: {
                        if (inst->semantic_kind == dgtl::machine::MachineSemanticKind::FloatSqrt) {
                            asm_out << "    v_sqrt_f32 v" << vd << ", v" << vs1 << "\n";
                        } else {
                            const char* op_str = "v_add_f32";
                            switch (inst->semantic_kind) {
                                case dgtl::machine::MachineSemanticKind::FloatAdd: op_str = "v_add_f32"; break;
                                case dgtl::machine::MachineSemanticKind::FloatSub: op_str = "v_sub_f32"; break;
                                case dgtl::machine::MachineSemanticKind::FloatMul: op_str = "v_mul_f32"; break;
                                case dgtl::machine::MachineSemanticKind::FloatDiv: op_str = "v_div_f32"; break;
                                default: break;
                            }
                            asm_out << "    " << op_str << " v" << vd << ", v" << vs1 << ", v" << vs2 << "\n";
                        }
                        break;
                    }
                    case dgtl::machine::MachineForm::FloatBinary64: {
                        if (inst->semantic_kind == dgtl::machine::MachineSemanticKind::FloatSqrt) {
                            asm_out << "    v_sqrt_f64 v" << vd << ", v" << vs1 << "\n";
                        } else {
                            const char* op_str = "v_add_f64";
                            switch (inst->semantic_kind) {
                                case dgtl::machine::MachineSemanticKind::FloatAdd: op_str = "v_add_f64"; break;
                                case dgtl::machine::MachineSemanticKind::FloatSub: op_str = "v_sub_f64"; break;
                                case dgtl::machine::MachineSemanticKind::FloatMul: op_str = "v_mul_f64"; break;
                                case dgtl::machine::MachineSemanticKind::FloatDiv: op_str = "v_div_f64"; break;
                                default: break;
                            }
                            asm_out << "    " << op_str << " v" << vd << ", v" << vs1 << ", v" << vs2 << "\n";
                        }
                        break;
                    }
                    case dgtl::machine::MachineForm::FloatTernaryFma32: {
                        if (inst->opcode == dgtl::machine::GM_FFMA64) {
                            asm_out << "    v_fma_f64 v" << vd << ", v" << vs1 << ", v" << vs2 << ", v" << vs3 << "\n";
                        } else {
                            asm_out << "    v_fma_f32 v" << vd << ", v" << vs1 << ", v" << vs2 << ", v" << vs3 << "\n";
                        }
                        break;
                    }
                    case dgtl::machine::MachineForm::LoadStack: {
                        const bool is_64 = (inst->opcode == dgtl::machine::GM_FLOAD64 || inst->opcode == dgtl::machine::GM_ILOAD64);
                        if (is_64) {
                            asm_out << "    flat_load_dwordx2 v[" << vd << ":" << (vd + 1) << "], v[" << vs1 << ":" << (vs1 + 1) << "] offset:" << imm << "\n";
                        } else {
                            asm_out << "    flat_load_dword v" << vd << ", v[" << vs1 << ":" << (vs1 + 1) << "] offset:" << imm << "\n";
                        }
                        break;
                    }
                    case dgtl::machine::MachineForm::StoreStack: {
                        const uint32_t val = (vd != 0 ? vd : vs2);
                        const bool is_64 = (inst->opcode == dgtl::machine::GM_FSTORE64 || inst->opcode == dgtl::machine::GM_ISTORE64);
                        if (is_64) {
                            asm_out << "    flat_store_dwordx2 v[" << vs1 << ":" << (vs1 + 1) << "], v[" << val << ":" << (val + 1) << "] offset:" << imm << "\n";
                        } else {
                            asm_out << "    flat_store_dword v[" << vs1 << ":" << (vs1 + 1) << "], v" << val << " offset:" << imm << "\n";
                        }
                        break;
                    }
                    case dgtl::machine::MachineForm::Compare32: {
                        const auto pred = inst->comparison_predicate();
                        const bool is_i64 = (inst->opcode == dgtl::machine::GM_ICMP64 ||
                                            (inst->opcode >= dgtl::machine::GM_ICMP64_EQ && inst->opcode <= dgtl::machine::GM_ICMP64_GE));
                        const bool is_f32 = (inst->opcode == dgtl::machine::GM_FCMP32 ||
                                            (inst->opcode >= dgtl::machine::GM_FCMP32_EQ && inst->opcode <= dgtl::machine::GM_FCMP32_GE) ||
                                            inst->semantic_kind == dgtl::machine::MachineSemanticKind::FloatCmp);
                        const bool is_f64 = (inst->opcode == dgtl::machine::GM_FCMP64 ||
                                            (inst->opcode >= dgtl::machine::GM_FCMP64_EQ && inst->opcode <= dgtl::machine::GM_FCMP64_GE));

                        const char* cmp_str = "eq";
                        switch (pred) {
                            case dgtl::machine::ComparisonPredicate::Equal: cmp_str = "eq"; break;
                            case dgtl::machine::ComparisonPredicate::NotEqual: cmp_str = "ne"; break;
                            case dgtl::machine::ComparisonPredicate::SignedLessThan: cmp_str = "lt"; break;
                            case dgtl::machine::ComparisonPredicate::SignedLessEqual: cmp_str = "le"; break;
                            case dgtl::machine::ComparisonPredicate::SignedGreaterThan: cmp_str = "gt"; break;
                            case dgtl::machine::ComparisonPredicate::SignedGreaterEqual: cmp_str = "ge"; break;
                            default: cmp_str = "eq"; break;
                        }

                        if (is_f64) {
                            asm_out << "    v_cmp_" << cmp_str << "_f64 vcc, v[" << vs1 << ":" << (vs1 + 1) << "], v[" << vs2 << ":" << (vs2 + 1) << "]\n";
                        } else if (is_f32) {
                            asm_out << "    v_cmp_" << cmp_str << "_f32 vcc, v" << vs1 << ", v" << vs2 << "\n";
                        } else if (is_i64) {
                            asm_out << "    v_cmp_" << cmp_str << "_i64 vcc, v[" << vs1 << ":" << (vs1 + 1) << "], v[" << vs2 << ":" << (vs2 + 1) << "]\n";
                        } else {
                            asm_out << "    v_cmp_" << cmp_str << "_i32 vcc, v" << vs1 << ", v" << vs2 << "\n";
                        }
                        if (vd != 0) {
                            asm_out << "    v_cndmask_b32 v" << vd << ", 0, 1, vcc\n";
                        }
                        break;
                    }
                    case dgtl::machine::MachineForm::TensorOp: {
                        asm_out << "    v_max_f32 v" << vd << ", 0.0, v" << vs1 << "\n";
                        break;
                    }
                    case dgtl::machine::MachineForm::Call: {
                        const char* sym = dgtl::mir::runtime_contract::runtime_symbol_for_semantic_kind(inst->semantic_kind);
                        if (!sym) sym = dgtl::mir::runtime_contract::runtime_symbol_for_opcode(inst->opcode);
                        if (sym) {
                            asm_out << "    ; Call runtime intrinsic: " << sym << "\n";
                            asm_out << "    s_getpc_b64 s[0:1]\n";
                            asm_out << "    s_swappc_b64 s[30:31], s[0:1]\n";
                        } else {
                            asm_out << "    ; Device subroutine call\n";
                            asm_out << "    s_getpc_b64 s[0:1]\n";
                            asm_out << "    s_swappc_b64 s[30:31], s[0:1]\n";
                        }
                        break;
                    }
                    case dgtl::machine::MachineForm::Goto:
                        if (!inst->operands.empty() && inst->operands[0].kind == dgtl::machine::MachineOperandKind::BasicBlock) {
                            asm_out << "    s_branch .L__bb_" << inst->operands[0].block << "\n";
                        }
                        break;
                    case dgtl::machine::MachineForm::BranchCond:
                        if (inst->operands.size() >= 2 && inst->operands[1].kind == dgtl::machine::MachineOperandKind::BasicBlock) {
                            asm_out << "    v_cmp_ne_u32 vcc, 0, v" << vs1 << "\n";
                            asm_out << "    s_cbranch_vccnz .L__bb_" << inst->operands[1].block << "\n";
                        }
                        break;
                    case dgtl::machine::MachineForm::BranchCompare:
                        if (inst->operands.size() >= 3 && inst->operands[2].kind == dgtl::machine::MachineOperandKind::BasicBlock) {
                            const char* cmp_op = "eq_u32";
                            switch (inst->comparison_predicate()) {
                                case dgtl::machine::ComparisonPredicate::Equal: cmp_op = "eq_u32"; break;
                                case dgtl::machine::ComparisonPredicate::NotEqual: cmp_op = "ne_u32"; break;
                                case dgtl::machine::ComparisonPredicate::SignedLessThan: cmp_op = "lt_i32"; break;
                                case dgtl::machine::ComparisonPredicate::SignedGreaterEqual: cmp_op = "ge_i32"; break;
                                default: break;
                            }
                            asm_out << "    v_cmp_" << cmp_op << " vcc, v" << vs1 << ", v" << vs2 << "\n";
                            asm_out << "    s_cbranch_vccnz .L__bb_" << inst->operands[2].block << "\n";
                        }
                        break;
                    case dgtl::machine::MachineForm::ConcurrentSync:
                        asm_out << "    s_barrier\n";
                        break;
                    case dgtl::machine::MachineForm::HardwareFence:
                        asm_out << "    s_waitcnt 0\n";
                        break;
                    default:
                        asm_out << "    ; unhandled form: " << dgtl::machine::machine_form_name(inst->form) << "\n";
                        break;
        }
    }

    [[nodiscard]] dgtl::machine::EmissionResult
    emit(const dgtl::machine::MachineFunction& function) override {
        dgtl::machine::EmissionResult result{};
        std::ostringstream asm_out;

        asm_out << "; DGTL Generated AMDGPU GCN/CDNA Assembly\n";
        asm_out << ".amdgcn_target \"amdgcn-amd-amdhsa--gfx90a\"\n";
        asm_out << ".text\n";
        asm_out << ".globl " << function.name() << "\n";
        asm_out << ".p2align 8\n";
        asm_out << ".type " << function.name() << ",@function\n";
        asm_out << function.name() << ":\n";

        for (const auto& block : function.blocks()) {
            asm_out << ".L__bb_" << block.id << ":\n";
            for (auto instr_id : block.instructions) {
                const auto* inst = function.instruction(instr_id);
                if (!inst) continue;
                emit_instruction_text(*inst, asm_out);
            }
        }

        asm_out << "    s_endpgm\n";
        asm_out << ".Lfunc_end0:\n";
        asm_out << "    .size " << function.name() << ", .Lfunc_end0-" << function.name() << "\n";

        std::string str = asm_out.str();
        result.bytes.assign(str.begin(), str.end());
        result.success = true;
        return result;
    }
};

using AMDGPUEmitter = ROCmEmitter;

} // namespace dgtl::isa::amdgpu