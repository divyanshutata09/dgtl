/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Component:   CUDA / PTX High-Performance Code Emitter
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

export module dgtl.isa.ptx.emitter;

import dgtl.mir.machine_ir;
import dgtl.mir.runtime_contract;

export namespace dgtl::isa::ptx {

class CUDAEmitter final : public dgtl::machine::MachineEmitter,
                          public dgtl::machine::MachineCodeEmitter {
public:
    [[nodiscard]] dgtl::machine::EmissionKind kind() const noexcept override {
        return dgtl::machine::EmissionKind::Assembly;
    }

    void emit_instruction(const dgtl::machine::MachineInstruction& inst,
                          std::vector<std::uint8_t>& out) override {
        std::ostringstream ptx;
        emit_instruction_text(inst, ptx);
        std::string str = ptx.str();
        out.insert(out.end(), str.begin(), str.end());
    }

    static void emit_instruction_text(const dgtl::machine::MachineInstruction& inst_ref, std::ostream& ptx) {
        const auto* inst = &inst_ref;
        uint32_t rd = 0, rs1 = 0, rs2 = 0, rs3 = 0;
        int64_t imm = 0;
        for (const auto& op : inst->operands) {
            if (op.kind == dgtl::machine::MachineOperandKind::Register) {
                if (op.role == dgtl::machine::OperandRole::Def) {
                    rd = op.reg.id & 0x3F;
                } else if (rs1 == 0) {
                    rs1 = op.reg.id & 0x3F;
                } else if (rs2 == 0) {
                    rs2 = op.reg.id & 0x3F;
                } else {
                    rs3 = op.reg.id & 0x3F;
                }
            } else if (op.kind == dgtl::machine::MachineOperandKind::Immediate) {
                imm = op.immediate;
            }
        }

        switch (inst->form) {
                    case dgtl::machine::MachineForm::NoOp:
                        break;
                    case dgtl::machine::MachineForm::Return:
                        ptx << "    ret;\n";
                        break;
                    case dgtl::machine::MachineForm::Move:
                        ptx << "    mov.b32 %r" << rd << ", %r" << rs1 << ";\n";
                        break;
                    case dgtl::machine::MachineForm::LoadImmediate:
                        ptx << "    mov.u32 %r" << rd << ", " << imm << ";\n";
                        break;
                    case dgtl::machine::MachineForm::IntegerBinary32: {
                        const char* op_str = "add.s32";
                        switch (inst->semantic_kind) {
                            case dgtl::machine::MachineSemanticKind::IntegerAdd: op_str = "add.s32"; break;
                            case dgtl::machine::MachineSemanticKind::IntegerSub: op_str = "sub.s32"; break;
                            case dgtl::machine::MachineSemanticKind::IntegerMul: op_str = "mul.lo.s32"; break;
                            case dgtl::machine::MachineSemanticKind::IntegerDiv: op_str = "div.s32"; break;
                            case dgtl::machine::MachineSemanticKind::IntegerAnd: op_str = "and.b32"; break;
                            case dgtl::machine::MachineSemanticKind::IntegerOr:  op_str = "or.b32"; break;
                            case dgtl::machine::MachineSemanticKind::IntegerXor: op_str = "xor.b32"; break;
                            case dgtl::machine::MachineSemanticKind::IntegerShl:  op_str = "shl.b32"; break;
                            case dgtl::machine::MachineSemanticKind::IntegerShr: op_str = "shr.s32"; break;
                            default: break;
                        }
                        ptx << "    " << op_str << " %r" << rd << ", %r" << rs1 << ", %r" << rs2 << ";\n";
                        break;
                    }
                    case dgtl::machine::MachineForm::IntegerBinary64: {
                        const char* op_str = "add.s64";
                        switch (inst->semantic_kind) {
                            case dgtl::machine::MachineSemanticKind::IntegerAdd: op_str = "add.s64"; break;
                            case dgtl::machine::MachineSemanticKind::IntegerSub: op_str = "sub.s64"; break;
                            case dgtl::machine::MachineSemanticKind::IntegerMul: op_str = "mul.lo.s64"; break;
                            case dgtl::machine::MachineSemanticKind::IntegerDiv: op_str = "div.s64"; break;
                            case dgtl::machine::MachineSemanticKind::IntegerAnd: op_str = "and.b64"; break;
                            case dgtl::machine::MachineSemanticKind::IntegerOr:  op_str = "or.b64"; break;
                            case dgtl::machine::MachineSemanticKind::IntegerXor: op_str = "xor.b64"; break;
                            case dgtl::machine::MachineSemanticKind::IntegerShl: op_str = "shl.b64"; break;
                            case dgtl::machine::MachineSemanticKind::IntegerShr: op_str = "shr.s64"; break;
                            default: break;
                        }
                        ptx << "    " << op_str << " %rd" << rd << ", %rd" << rs1 << ", %rd" << rs2 << ";\n";
                        break;
                    }
                    case dgtl::machine::MachineForm::FloatBinary32: {
                        if (inst->semantic_kind == dgtl::machine::MachineSemanticKind::FloatSqrt || inst->opcode == dgtl::machine::GM_FSQRT32) {
                            ptx << "    sqrt.rn.f32 %f" << rd << ", %f" << rs1 << ";\n";
                        } else {
                            const char* op_str = "add.f32";
                            switch (inst->semantic_kind) {
                                case dgtl::machine::MachineSemanticKind::FloatAdd: op_str = "add.f32"; break;
                                case dgtl::machine::MachineSemanticKind::FloatSub: op_str = "sub.f32"; break;
                                case dgtl::machine::MachineSemanticKind::FloatMul: op_str = "mul.f32"; break;
                                case dgtl::machine::MachineSemanticKind::FloatDiv: op_str = "div.rn.f32"; break;
                                default: {
                                    if (inst->opcode == dgtl::machine::GM_FSUB32) op_str = "sub.f32";
                                    else if (inst->opcode == dgtl::machine::GM_FMUL32) op_str = "mul.f32";
                                    else if (inst->opcode == dgtl::machine::GM_FDIV32) op_str = "div.rn.f32";
                                    break;
                                }
                            }
                            ptx << "    " << op_str << " %f" << rd << ", %f" << rs1 << ", %f" << rs2 << ";\n";
                        }
                        break;
                    }
                    case dgtl::machine::MachineForm::FloatBinary64: {
                        if (inst->semantic_kind == dgtl::machine::MachineSemanticKind::FloatSqrt || inst->opcode == dgtl::machine::GM_FSQRT64) {
                            ptx << "    sqrt.rn.f64 %fd" << rd << ", %fd" << rs1 << ";\n";
                        } else {
                            const char* op_str = "add.f64";
                            switch (inst->semantic_kind) {
                                case dgtl::machine::MachineSemanticKind::FloatAdd: op_str = "add.f64"; break;
                                case dgtl::machine::MachineSemanticKind::FloatSub: op_str = "sub.f64"; break;
                                case dgtl::machine::MachineSemanticKind::FloatMul: op_str = "mul.f64"; break;
                                case dgtl::machine::MachineSemanticKind::FloatDiv: op_str = "div.rn.f64"; break;
                                default: {
                                    if (inst->opcode == dgtl::machine::GM_FSUB64) op_str = "sub.f64";
                                    else if (inst->opcode == dgtl::machine::GM_FMUL64) op_str = "mul.f64";
                                    else if (inst->opcode == dgtl::machine::GM_FDIV64) op_str = "div.rn.f64";
                                    break;
                                }
                            }
                            ptx << "    " << op_str << " %fd" << rd << ", %fd" << rs1 << ", %fd" << rs2 << ";\n";
                        }
                        break;
                    }
                    case dgtl::machine::MachineForm::FloatTernaryFma32: {
                        const bool is_f64 = (inst->opcode == dgtl::machine::GM_FFMA64);
                        if (is_f64) {
                            ptx << "    fma.rn.f64 %fd" << rd << ", %fd" << rs1 << ", %fd" << rs2 << ", %fd" << rs3 << ";\n";
                        } else {
                            ptx << "    fma.rn.f32 %f" << rd << ", %f" << rs1 << ", %f" << rs2 << ", %f" << rs3 << ";\n";
                        }
                        break;
                    }
                    case dgtl::machine::MachineForm::LoadStack: {
                        const bool is_fp = (inst->semantic_kind == dgtl::machine::MachineSemanticKind::FloatLoad ||
                                            inst->opcode == dgtl::machine::GM_FLOAD32 || inst->opcode == dgtl::machine::GM_FLOAD64);
                        if (is_fp && inst->opcode == dgtl::machine::GM_FLOAD64) {
                            ptx << "    ld.local.f64 %fd" << rd << ", [%rd" << rs1 << " + " << imm << "];\n";
                        } else if (is_fp) {
                            ptx << "    ld.local.f32 %f" << rd << ", [%rd" << rs1 << " + " << imm << "];\n";
                        } else if (inst->opcode == dgtl::machine::GM_ILOAD64) {
                            ptx << "    ld.local.u64 %rd" << rd << ", [%rd" << rs1 << " + " << imm << "];\n";
                        } else {
                            ptx << "    ld.local.u32 %r" << rd << ", [%rd" << rs1 << " + " << imm << "];\n";
                        }
                        break;
                    }
                    case dgtl::machine::MachineForm::StoreStack: {
                        const uint32_t val = (rd != 0 ? rd : rs2);
                        const bool is_fp = (inst->semantic_kind == dgtl::machine::MachineSemanticKind::FloatStore ||
                                            inst->opcode == dgtl::machine::GM_FSTORE32 || inst->opcode == dgtl::machine::GM_FSTORE64);
                        if (is_fp && inst->opcode == dgtl::machine::GM_FSTORE64) {
                            ptx << "    st.local.f64 [%rd" << rs1 << " + " << imm << "], %fd" << val << ";\n";
                        } else if (is_fp) {
                            ptx << "    st.local.f32 [%rd" << rs1 << " + " << imm << "], %f" << val << ";\n";
                        } else if (inst->opcode == dgtl::machine::GM_ISTORE64) {
                            ptx << "    st.local.u64 [%rd" << rs1 << " + " << imm << "], %rd" << val << ";\n";
                        } else {
                            ptx << "    st.local.u32 [%rd" << rs1 << " + " << imm << "], %r" << val << ";\n";
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
                            case dgtl::machine::ComparisonPredicate::UnsignedLessThan: cmp_str = "lo"; break;
                            case dgtl::machine::ComparisonPredicate::UnsignedLessEqual: cmp_str = "ls"; break;
                            case dgtl::machine::ComparisonPredicate::UnsignedGreaterThan: cmp_str = "hi"; break;
                            case dgtl::machine::ComparisonPredicate::UnsignedGreaterEqual: cmp_str = "hs"; break;
                            default: cmp_str = "eq"; break;
                        }

                        if (is_f64) {
                            ptx << "    setp." << cmp_str << ".f64 %p0, %fd" << rs1 << ", %fd" << rs2 << ";\n";
                        } else if (is_f32) {
                            ptx << "    setp." << cmp_str << ".f32 %p0, %f" << rs1 << ", %f" << rs2 << ";\n";
                        } else if (is_i64) {
                            ptx << "    setp." << cmp_str << ".s64 %p0, %rd" << rs1 << ", %rd" << rs2 << ";\n";
                        } else {
                            ptx << "    setp." << cmp_str << ".s32 %p0, %r" << rs1 << ", %r" << rs2 << ";\n";
                        }
                        if (rd != 0) {
                            ptx << "    selp.u32 %r" << rd << ", 1, 0, %p0;\n";
                        }
                        break;
                    }
                    case dgtl::machine::MachineForm::TensorOp: {
                        ptx << "    max.f32 %f" << rd << ", %f" << rs1 << ", 0.0;\n";
                        break;
                    }
                    case dgtl::machine::MachineForm::Call: {
                        const char* sym = dgtl::mir::runtime_contract::runtime_symbol_for_semantic_kind(inst->semantic_kind);
                        if (!sym) sym = dgtl::mir::runtime_contract::runtime_symbol_for_opcode(inst->opcode);
                        if (sym) {
                            ptx << "    // Call runtime intrinsic: " << sym << "\n";
                            ptx << "    call (%r" << rd << "), " << sym << ", (%r" << rs1 << ");\n";
                        } else {
                            ptx << "    // Call external/device target\n";
                            ptx << "    call (%r" << rd << "), dev_call, (%r" << rs1 << ");\n";
                        }
                        break;
                    }
                    case dgtl::machine::MachineForm::Goto:
                        if (!inst->operands.empty() && inst->operands[0].kind == dgtl::machine::MachineOperandKind::BasicBlock) {
                            ptx << "    bra $L__bb_" << inst->operands[0].block << ";\n";
                        }
                        break;
                    case dgtl::machine::MachineForm::BranchCond:
                        if (inst->operands.size() >= 2 && inst->operands[1].kind == dgtl::machine::MachineOperandKind::BasicBlock) {
                            ptx << "    setp.ne.s32 %p0, %r" << rs1 << ", 0;\n";
                            ptx << "    @%p0 bra $L__bb_" << inst->operands[1].block << ";\n";
                        }
                        break;
                    case dgtl::machine::MachineForm::BranchCompare:
                        if (inst->operands.size() >= 3 && inst->operands[2].kind == dgtl::machine::MachineOperandKind::BasicBlock) {
                            const char* cmp_op = "eq";
                            switch (inst->comparison_predicate()) {
                                case dgtl::machine::ComparisonPredicate::Equal: cmp_op = "eq"; break;
                                case dgtl::machine::ComparisonPredicate::NotEqual: cmp_op = "ne"; break;
                                case dgtl::machine::ComparisonPredicate::SignedLessThan: cmp_op = "lt"; break;
                                case dgtl::machine::ComparisonPredicate::SignedGreaterEqual: cmp_op = "ge"; break;
                                default: break;
                            }
                            ptx << "    setp." << cmp_op << ".s32 %p0, %r" << rs1 << ", %r" << rs2 << ";\n";
                            ptx << "    @%p0 bra $L__bb_" << inst->operands[2].block << ";\n";
                        }
                        break;
                    case dgtl::machine::MachineForm::ConcurrentSync:
                        ptx << "    bar.sync 0;\n";
                        break;
                    case dgtl::machine::MachineForm::HardwareFence:
                        ptx << "    membar.gl;\n";
                        break;
                    default:
                        ptx << "    // unhandled form: " << dgtl::machine::machine_form_name(inst->form) << "\n";
                        break;
        }
    }

    [[nodiscard]] dgtl::machine::EmissionResult
    emit(const dgtl::machine::MachineFunction& function) override {
        dgtl::machine::EmissionResult result{};
        std::ostringstream ptx;

        ptx << "// DGTL Generated NVIDIA PTX Assembly\n";
        ptx << ".version 7.8\n";
        ptx << ".target sm_80\n";
        ptx << ".address_size 64\n\n";

        ptx << ".visible .entry " << function.name() << "(\n";
        ptx << "    .param .u64 __param_0\n";
        ptx << ") {\n";
        ptx << "    .reg .b64 %rd<64>;\n";
        ptx << "    .reg .b32 %r<64>;\n";
        ptx << "    .reg .f32 %f<64>;\n";
        ptx << "    .reg .f64 %fd<64>;\n\n";

        ptx << "    .reg .pred %p<16>;\n\n";

        for (const auto& block : function.blocks()) {
            ptx << "$L__bb_" << block.id << ":\n";
            for (auto instr_id : block.instructions) {
                const auto* inst = function.instruction(instr_id);
                if (!inst) continue;
                emit_instruction_text(*inst, ptx);
            }
        }

        ptx << "    ret;\n";
        ptx << "}\n";

        std::string str = ptx.str();
        result.bytes.assign(str.begin(), str.end());
        result.success = true;
        return result;
    }
};

} // namespace dgtl::isa::ptx