/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Component:   ARM64 / AArch64 Target Instruction Metadata & Legality Contract
 * Standards:   C++26 / Clang 18+ / MSVC 19.40+ / GCC 14+
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */
module;

#include <cstdlib>
#include <cstdint>

export module dgtl.isa.arm64.arm64_instruction_info;

import dgtl.mir.machine_ir;

export namespace dgtl::isa::arm64 {

[[nodiscard]] constexpr dgtl::machine::MachineForm
form_for_generic_opcode(dgtl::machine::MachineOpcode opcode) noexcept {
    using namespace dgtl::machine;
    switch (opcode) {
        case GM_NOOP: return MachineForm::NoOp;
        case GM_COPY: return MachineForm::Move;
        case GM_IMMEDIATE: return MachineForm::LoadImmediate;
        case GM_LOAD_STACK: return MachineForm::LoadStack;
        case GM_STORE_STACK: return MachineForm::StoreStack;
        case GM_IADD32: case GM_ISUB32: case GM_IMUL32: case GM_IDIV32: case GM_IREM32:
        case GM_IAND32: case GM_IOR32: case GM_IXOR32: case GM_ISHL32: case GM_ISHR32:
            return MachineForm::IntegerBinary32;
        case GM_IADD64: case GM_ISUB64: case GM_IMUL64: case GM_IDIV64: case GM_IREM64:
        case GM_IAND64: case GM_IOR64: case GM_IXOR64: case GM_ISHL64: case GM_ISHR64:
            return MachineForm::IntegerBinary64;
        case GM_FADD32: case GM_FSUB32: case GM_FMUL32: case GM_FDIV32:
            return MachineForm::FloatBinary32;
        case GM_FADD64: case GM_FSUB64: case GM_FMUL64: case GM_FDIV64:
            return MachineForm::FloatBinary64;
        case GM_FFMA32: case GM_FFMA64:
            return MachineForm::FloatTernaryFma32;
        case GM_ICMP32: case GM_ICMP64: case GM_FCMP32: case GM_FCMP64:
            return MachineForm::Compare32;
        case GM_GOTO: return MachineForm::Goto;
        case GM_BR_COND: return MachineForm::BranchCond;
        case GM_BR_COMPARE: return MachineForm::BranchCompare;
        case GM_CALL: case GM_CALL_VARIADIC: return MachineForm::Call;
        case GM_RETURN: return MachineForm::Return;
        case GM_SYSCALL: return MachineForm::Syscall;
        case GM_LOAD: return MachineForm::LoadStack;
        case GM_STORE: return MachineForm::StoreStack;
        case GM_VEC_LOAD: case GM_VEC_GATHER: return MachineForm::VectorGather512;
        case GM_VEC_STORE: case GM_VEC_SCATTER: return MachineForm::VectorStore512;
        case GM_FENCE: case GM_ACQUIRE_FENCE: case GM_RELEASE_FENCE: case GM_ATOMIC_FENCE:
            return MachineForm::HardwareFence;
        case GM_ATOMIC_LOAD: case GM_ATOMIC_STORE: case GM_ATOMIC_SNAPSHOT:
            return MachineForm::AtomicOp;
        case GM_BARRIER_SYNC: case GM_SYNC_BARRIER:
            return MachineForm::ConcurrentSync;
        case GM_TENSOR_OP: case GM_TENSOR_GEMM: case GM_TENSOR_CONV:
            return MachineForm::TensorOp;
        default:
            return MachineForm::Unhandled;
    }
}

class ARM64TargetInstructionInfo final :
    public dgtl::machine::CommonTargetInstructionInfo {
public:
    [[nodiscard]] dgtl::machine::MachineOpcode
    opcode_for(dgtl::machine::MachineForm form,
               dgtl::machine::MachineSemanticKind semantic_kind) const noexcept override {
        using namespace dgtl::machine;
        switch (form) {
            case MachineForm::NoOp: return GM_NOOP;
            case MachineForm::Move: return GM_COPY;
            case MachineForm::LoadImmediate: return GM_IMMEDIATE;
            case MachineForm::LoadStack: return GM_LOAD_STACK;
            case MachineForm::StoreStack: return GM_STORE_STACK;
            case MachineForm::IntegerBinary32:
                switch (semantic_kind) {
                    case MachineSemanticKind::IntegerAdd: return GM_IADD32;
                    case MachineSemanticKind::IntegerSub: return GM_ISUB32;
                    case MachineSemanticKind::IntegerMul: return GM_IMUL32;
                    case MachineSemanticKind::IntegerDiv: return GM_IDIV32;
                    case MachineSemanticKind::IntegerMod: return GM_IREM32;
                    case MachineSemanticKind::IntegerAnd: return GM_IAND32;
                    case MachineSemanticKind::IntegerOr:  return GM_IOR32;
                    case MachineSemanticKind::IntegerXor: return GM_IXOR32;
                    case MachineSemanticKind::IntegerShl: return GM_ISHL32;
                    case MachineSemanticKind::IntegerShr: return GM_ISHR32;
                    default: return GM_IADD32;
                }
            case MachineForm::IntegerBinary64:
                switch (semantic_kind) {
                    case MachineSemanticKind::IntegerAdd: return GM_IADD64;
                    case MachineSemanticKind::IntegerSub: return GM_ISUB64;
                    case MachineSemanticKind::IntegerMul: return GM_IMUL64;
                    case MachineSemanticKind::IntegerDiv: return GM_IDIV64;
                    case MachineSemanticKind::IntegerMod: return GM_IREM64;
                    case MachineSemanticKind::IntegerAnd: return GM_IAND64;
                    case MachineSemanticKind::IntegerOr:  return GM_IOR64;
                    case MachineSemanticKind::IntegerXor: return GM_IXOR64;
                    case MachineSemanticKind::IntegerShl: return GM_ISHL64;
                    case MachineSemanticKind::IntegerShr: return GM_ISHR64;
                    default: return GM_IADD64;
                }
            case MachineForm::FloatBinary32:
                switch (semantic_kind) {
                    case MachineSemanticKind::FloatAdd: return GM_FADD32;
                    case MachineSemanticKind::FloatSub: return GM_FSUB32;
                    case MachineSemanticKind::FloatMul: return GM_FMUL32;
                    case MachineSemanticKind::FloatDiv: return GM_FDIV32;
                    default: return GM_FADD32;
                }
            case MachineForm::FloatBinary64:
                switch (semantic_kind) {
                    case MachineSemanticKind::FloatAdd: return GM_FADD64;
                    case MachineSemanticKind::FloatSub: return GM_FSUB64;
                    case MachineSemanticKind::FloatMul: return GM_FMUL64;
                    case MachineSemanticKind::FloatDiv: return GM_FDIV64;
                    default: return GM_FADD64;
                }
            case MachineForm::FloatTernaryFma32: return GM_FFMA32;
            case MachineForm::Compare32: return GM_ICMP32;
            case MachineForm::Goto: return GM_GOTO;
            case MachineForm::BranchCond: return GM_BR_COND;
            case MachineForm::BranchCompare: return GM_BR_COMPARE;
            case MachineForm::Call: return GM_CALL;
            case MachineForm::Return: return GM_RETURN;
            case MachineForm::Syscall: return GM_SYSCALL;
            case MachineForm::VectorGather512: return GM_VEC_GATHER;
            case MachineForm::VectorStore512: return GM_VEC_STORE;
            case MachineForm::HardwareFence: return GM_FENCE;
            case MachineForm::AtomicOp: return GM_ATOMIC_LOAD;
            case MachineForm::ConcurrentSync: return GM_BARRIER_SYNC;
            case MachineForm::TensorOp: return GM_TENSOR_OP;
            default: return INVALID_MACHINE_OPCODE;
        }
    }

    [[nodiscard]] dgtl::machine::MachineInstructionInfo
    info(dgtl::machine::MachineOpcode opcode) const noexcept override {
        using namespace dgtl::machine;
        const MachineForm form = form_for_generic_opcode(opcode);
        return default_form_info(form);
    }

    [[nodiscard]] bool is_legal(
        dgtl::machine::MachineOpcode opcode) const noexcept override {
        using namespace dgtl::machine;
        return opcode != INVALID_MACHINE_OPCODE && opcode != GM_INVALID;
    }

    [[nodiscard]] bool is_copy(
        dgtl::machine::MachineOpcode opcode) const noexcept override {
        using namespace dgtl::machine;
        return opcode == GM_COPY;
    }

    [[nodiscard]] bool is_move(
        dgtl::machine::MachineOpcode opcode) const noexcept override {
        using namespace dgtl::machine;
        return opcode == GM_COPY;
    }

    [[nodiscard]] dgtl::machine::MachineOpcode
    load_opcode(dgtl::machine::RegisterClassKind reg_class) const noexcept override {
        using namespace dgtl::machine;
        switch (reg_class) {
            case RegisterClassKind::Vector:
                return GM_VEC_LOAD;
            default:
                return GM_LOAD;
        }
    }

    [[nodiscard]] dgtl::machine::MachineOpcode
    store_opcode(dgtl::machine::RegisterClassKind reg_class) const noexcept override {
        using namespace dgtl::machine;
        switch (reg_class) {
            case RegisterClassKind::Vector:
                return GM_VEC_STORE;
            default:
                return GM_STORE;
        }
    }
};

[[nodiscard]] inline dgtl::machine::MachineOpcode
opcode_for(dgtl::machine::MachineForm form,
           dgtl::machine::MachineSemanticKind semantic_kind) noexcept {
    static const ARM64TargetInstructionInfo info_instance{};
    return info_instance.opcode_for(form, semantic_kind);
}

} // namespace dgtl::isa::arm64
