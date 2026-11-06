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
#include <stdexcept>

export module dgtl.isa.arm64.arm64_instruction_info;

import dgtl.mir.machine_ir;

export namespace dgtl::isa::arm64 {

// Stable target-opcode identities for ARM64 (AArch64).
enum class ARM64MachineOpcode : dgtl::machine::MachineOpcode {
    Nop = dgtl::machine::TARGET_OPCODE_START,
    Move,
    MoveImmediate,
    Load,
    Store,
    IAdd32, ISub32, IMul32, IDiv32, IRem32, IAnd32, IOr32, IXor32, IShl32, IShr32,
    IAdd64, ISub64, IMul64, IDiv64, IRem64, IAnd64, IOr64, IXor64, IShl64, IShr64,
    FAdd32, FSub32, FMul32, FDiv32, FSqrt32,
    FAdd64, FSub64, FMul64, FDiv64, FSqrt64,
    Fma32, Fma64,
    Compare32,
    Jump, BranchCond, BranchCompare, Call, Return, Syscall,
    VectorGather512, VectorStore512,
    Fence, Atomic, ConcurrentSync, Tensor
};

[[nodiscard]] constexpr dgtl::machine::MachineOpcode
arm64_opcode(ARM64MachineOpcode opcode) noexcept {
    return static_cast<dgtl::machine::MachineOpcode>(opcode);
}

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
        case GM_FADD32: case GM_FSUB32: case GM_FMUL32: case GM_FDIV32: case GM_FSQRT32:
            return MachineForm::FloatBinary32;
        case GM_FADD64: case GM_FSUB64: case GM_FMUL64: case GM_FDIV64: case GM_FSQRT64:
            return MachineForm::FloatBinary64;
        case GM_FFMA32: case GM_FFMA64:
            return MachineForm::FloatTernaryFma32;
        case GM_ICMP32: case GM_ICMP64: case GM_FCMP32: case GM_FCMP64:
            return MachineForm::Compare32;
        case GM_GOTO: return MachineForm::Goto;
        case GM_BR_COND: return MachineForm::BranchCond;
        case GM_BR_COMPARE: return MachineForm::BranchCompare;
        case GM_CALL: case GM_CALL_VARIADIC:
        case GM_FSIN32: case GM_FCOS32: case GM_FTAN32: case GM_FEXP32: case GM_FLOG32: case GM_FPOW32:
        case GM_FSIN64: case GM_FCOS64: case GM_FTAN64: case GM_FEXP64: case GM_FLOG64: case GM_FPOW64:
            return MachineForm::Call;
        case GM_RETURN: return MachineForm::Return;
        case GM_SYSCALL: return MachineForm::Syscall;
        case GM_LOAD:
        case GM_ILOAD32: case GM_ILOAD64: case GM_FLOAD32: case GM_FLOAD64:
            return MachineForm::LoadStack;
        case GM_STORE:
        case GM_ISTORE32: case GM_ISTORE64: case GM_FSTORE32: case GM_FSTORE64:
            return MachineForm::StoreStack;
        case GM_VEC_LOAD: case GM_VEC_GATHER: return MachineForm::VectorGather512;
        case GM_VEC_STORE: case GM_VEC_SCATTER: return MachineForm::VectorStore512;
        case GM_FENCE: case GM_ACQUIRE_FENCE: case GM_RELEASE_FENCE: case GM_ATOMIC_FENCE:
            return MachineForm::HardwareFence;
        case GM_ATOMIC_LOAD: case GM_ATOMIC_STORE: case GM_ATOMIC_SNAPSHOT:
            return MachineForm::AtomicOp;
        case GM_BARRIER_SYNC: case GM_SYNC_BARRIER:
            return MachineForm::ConcurrentSync;
        case GM_TENSOR_OP: case GM_TENSOR_GEMM: case GM_TENSOR_CONV: case GM_TENSOR_RELU: case GM_TENSOR_RELU64:
        case GM_TENSOR_BROADCAST: case GM_TENSOR_CONCAT: case GM_TENSOR_POOL: case GM_TENSOR_REDUCE:
        case GM_TENSOR_RESHAPE: case GM_TENSOR_SLICE: case GM_TENSOR_SOFTMAX: case GM_TENSOR_TRANSPOSE:
            return MachineForm::TensorOp;
        default:
            return MachineForm::Unhandled;
    }
}

[[nodiscard]] constexpr dgtl::machine::MachineForm
form_for_arm64_opcode(dgtl::machine::MachineOpcode opcode) noexcept {
    using namespace dgtl::machine;
    using A = ARM64MachineOpcode;
    switch (static_cast<A>(opcode)) {
        case A::Nop: return MachineForm::NoOp;
        case A::Move: return MachineForm::Move;
        case A::MoveImmediate: return MachineForm::LoadImmediate;
        case A::Load: return MachineForm::LoadStack;
        case A::Store: return MachineForm::StoreStack;
        case A::IAdd32: case A::ISub32: case A::IMul32: case A::IDiv32: case A::IRem32:
        case A::IAnd32: case A::IOr32: case A::IXor32: case A::IShl32: case A::IShr32:
            return MachineForm::IntegerBinary32;
        case A::IAdd64: case A::ISub64: case A::IMul64: case A::IDiv64: case A::IRem64:
        case A::IAnd64: case A::IOr64: case A::IXor64: case A::IShl64: case A::IShr64:
            return MachineForm::IntegerBinary64;
        case A::FAdd32: case A::FSub32: case A::FMul32: case A::FDiv32: case A::FSqrt32:
            return MachineForm::FloatBinary32;
        case A::FAdd64: case A::FSub64: case A::FMul64: case A::FDiv64: case A::FSqrt64:
            return MachineForm::FloatBinary64;
        case A::Fma32: case A::Fma64: return MachineForm::FloatTernaryFma32;
        case A::Compare32: return MachineForm::Compare32;
        case A::Jump: return MachineForm::Goto;
        case A::BranchCond: return MachineForm::BranchCond;
        case A::BranchCompare: return MachineForm::BranchCompare;
        case A::Call: return MachineForm::Call;
        case A::Return: return MachineForm::Return;
        case A::Syscall: return MachineForm::Syscall;
        case A::VectorGather512: return MachineForm::VectorGather512;
        case A::VectorStore512: return MachineForm::VectorStore512;
        case A::Fence: return MachineForm::HardwareFence;
        case A::Atomic: return MachineForm::AtomicOp;
        case A::ConcurrentSync: return MachineForm::ConcurrentSync;
        case A::Tensor: return MachineForm::TensorOp;
        default: return MachineForm::Unhandled;
    }
}

[[nodiscard]] constexpr bool is_arm64_opcode_for_form(
    dgtl::machine::MachineOpcode opcode,
    dgtl::machine::MachineForm form) noexcept {
    return dgtl::machine::is_target_opcode(opcode) &&
           form_for_arm64_opcode(opcode) == form;
}

class ARM64TargetInstructionInfo final :
    public dgtl::machine::CommonTargetInstructionInfo {
public:
    [[nodiscard]] dgtl::machine::MachineOpcode
    opcode_for(dgtl::machine::MachineForm form,
               dgtl::machine::MachineSemanticKind semantic_kind) const noexcept override {
        using namespace dgtl::machine;
        using A = ARM64MachineOpcode;
        const auto op = [](A value) noexcept { return arm64_opcode(value); };
        switch (form) {
            case MachineForm::NoOp: return op(A::Nop);
            case MachineForm::Move: return op(A::Move);
            case MachineForm::LoadImmediate: return op(A::MoveImmediate);
            case MachineForm::LoadStack: return op(A::Load);
            case MachineForm::StoreStack: return op(A::Store);
            case MachineForm::IntegerBinary32:
                switch (semantic_kind) {
                    case MachineSemanticKind::IntegerAdd: return op(A::IAdd32);
                    case MachineSemanticKind::IntegerSub: return op(A::ISub32);
                    case MachineSemanticKind::IntegerMul: return op(A::IMul32);
                    case MachineSemanticKind::IntegerDiv: return op(A::IDiv32);
                    case MachineSemanticKind::IntegerMod: return op(A::IRem32);
                    case MachineSemanticKind::IntegerAnd: return op(A::IAnd32);
                    case MachineSemanticKind::IntegerOr:  return op(A::IOr32);
                    case MachineSemanticKind::IntegerXor: return op(A::IXor32);
                    case MachineSemanticKind::IntegerShl: return op(A::IShl32);
                    case MachineSemanticKind::IntegerShr: return op(A::IShr32);
                    default: return INVALID_MACHINE_OPCODE;
                }
            case MachineForm::IntegerBinary64:
                switch (semantic_kind) {
                    case MachineSemanticKind::IntegerAdd: return op(A::IAdd64);
                    case MachineSemanticKind::IntegerSub: return op(A::ISub64);
                    case MachineSemanticKind::IntegerMul: return op(A::IMul64);
                    case MachineSemanticKind::IntegerDiv: return op(A::IDiv64);
                    case MachineSemanticKind::IntegerMod: return op(A::IRem64);
                    case MachineSemanticKind::IntegerAnd: return op(A::IAnd64);
                    case MachineSemanticKind::IntegerOr:  return op(A::IOr64);
                    case MachineSemanticKind::IntegerXor: return op(A::IXor64);
                    case MachineSemanticKind::IntegerShl: return op(A::IShl64);
                    case MachineSemanticKind::IntegerShr: return op(A::IShr64);
                    default: return INVALID_MACHINE_OPCODE;
                }
            case MachineForm::FloatBinary32:
                switch (semantic_kind) {
                    case MachineSemanticKind::FloatAdd: return op(A::FAdd32);
                    case MachineSemanticKind::FloatSub: return op(A::FSub32);
                    case MachineSemanticKind::FloatMul: return op(A::FMul32);
                    case MachineSemanticKind::FloatDiv: return op(A::FDiv32);
                    case MachineSemanticKind::FloatSqrt: return op(A::FSqrt32);
                    default: return INVALID_MACHINE_OPCODE;
                }
            case MachineForm::FloatBinary64:
                switch (semantic_kind) {
                    case MachineSemanticKind::FloatAdd: return op(A::FAdd64);
                    case MachineSemanticKind::FloatSub: return op(A::FSub64);
                    case MachineSemanticKind::FloatMul: return op(A::FMul64);
                    case MachineSemanticKind::FloatDiv: return op(A::FDiv64);
                    case MachineSemanticKind::FloatSqrt: return op(A::FSqrt64);
                    default: return INVALID_MACHINE_OPCODE;
                }
            case MachineForm::FloatTernaryFma32: return op(A::Fma32);
            case MachineForm::Compare32:
                return (semantic_kind == MachineSemanticKind::Compare || semantic_kind == MachineSemanticKind::FloatCmp) ? op(A::Compare32) : INVALID_MACHINE_OPCODE;
            case MachineForm::Goto: return op(A::Jump);
            case MachineForm::BranchCond: return op(A::BranchCond);
            case MachineForm::BranchCompare: return op(A::BranchCompare);
            case MachineForm::Call: return op(A::Call);
            case MachineForm::Return: return op(A::Return);
            case MachineForm::Syscall: return op(A::Syscall);
            case MachineForm::VectorGather512: return op(A::VectorGather512);
            case MachineForm::VectorStore512: return op(A::VectorStore512);
            case MachineForm::HardwareFence: return op(A::Fence);
            case MachineForm::AtomicOp: return op(A::Atomic);
            case MachineForm::ConcurrentSync: return op(A::ConcurrentSync);
            case MachineForm::TensorOp: return op(A::Tensor);
            default: return INVALID_MACHINE_OPCODE;
        }
    }

    [[nodiscard]] dgtl::machine::MachineInstructionInfo
    info(dgtl::machine::MachineOpcode opcode) const noexcept override {
        using namespace dgtl::machine;
        const MachineForm form = is_target_opcode(opcode)
            ? form_for_arm64_opcode(opcode)
            : form_for_generic_opcode(opcode);
        return default_form_info(form);
    }

    [[nodiscard]] bool is_legal(dgtl::machine::MachineOpcode opcode) const noexcept override {
        using namespace dgtl::machine;
        if (is_target_opcode(opcode)) {
            return form_for_arm64_opcode(opcode) != MachineForm::Unhandled;
        }
        return is_generic_opcode(opcode);
    }

    [[nodiscard]] bool is_copy(dgtl::machine::MachineOpcode opcode) const noexcept override {
        return opcode == dgtl::machine::GM_COPY || opcode == arm64_opcode(ARM64MachineOpcode::Move);
    }

    [[nodiscard]] bool is_move(dgtl::machine::MachineOpcode opcode) const noexcept override {
        return is_copy(opcode);
    }

    [[nodiscard]] dgtl::machine::MachineOpcode
    load_opcode(dgtl::machine::RegisterClassKind reg_class) const noexcept override {
        return reg_class == dgtl::machine::RegisterClassKind::Vector
            ? dgtl::machine::GM_VEC_LOAD : dgtl::machine::GM_LOAD;
    }

    [[nodiscard]] dgtl::machine::MachineOpcode
    store_opcode(dgtl::machine::RegisterClassKind reg_class) const noexcept override {
        return reg_class == dgtl::machine::RegisterClassKind::Vector
            ? dgtl::machine::GM_VEC_STORE : dgtl::machine::GM_STORE;
    }

    void store_reg_to_stack_slot(
        dgtl::machine::MachineFunction& function,
        dgtl::machine::BlockId block,
        dgtl::machine::InstrId cursor,
        dgtl::machine::MachineRegister src,
        dgtl::machine::StackSlotId slot) const override {
        using namespace dgtl::machine;
        MachineInstruction& inst = function.create_instruction(arm64_opcode(ARM64MachineOpcode::Store));
        inst.form = MachineForm::StoreStack;
        inst.semantic_kind = MachineSemanticKind::StoreStack;
        inst.add_flag(MachineInstrFlag::Store);
        inst.add_operand(MachineOperand::stack_ref(slot));
        inst.add_operand(MachineOperand::use(src));
        inst.parent_block = block;
        if (!function.insert_instruction_before(block, cursor, inst.id)) {
            throw std::logic_error("DGTL arm64 backend invariant violation: failed to insert stack store instruction");
        }
    }

    void load_reg_from_stack_slot(
        dgtl::machine::MachineFunction& function,
        dgtl::machine::BlockId block,
        dgtl::machine::InstrId cursor,
        dgtl::machine::MachineRegister dst,
        dgtl::machine::StackSlotId slot) const override {
        using namespace dgtl::machine;
        MachineInstruction& inst = function.create_instruction(arm64_opcode(ARM64MachineOpcode::Load));
        inst.form = MachineForm::LoadStack;
        inst.semantic_kind = MachineSemanticKind::LoadStack;
        inst.add_flag(MachineInstrFlag::Load);
        inst.add_operand(MachineOperand::def(dst));
        inst.add_operand(MachineOperand::stack_ref(slot));
        inst.parent_block = block;
        if (!function.insert_instruction_before(block, cursor, inst.id)) {
            throw std::logic_error("DGTL arm64 backend invariant violation: failed to insert stack load instruction");
        }
    }

    void copy_phys_reg(
        dgtl::machine::MachineFunction& function,
        dgtl::machine::BlockId block,
        dgtl::machine::InstrId cursor,
        dgtl::machine::MachineRegister dst,
        dgtl::machine::MachineRegister src) const override {
        using namespace dgtl::machine;
        MachineInstruction& inst = function.create_instruction(arm64_opcode(ARM64MachineOpcode::Move));
        inst.form = MachineForm::Move;
        inst.semantic_kind = MachineSemanticKind::Move;
        inst.add_operand(MachineOperand::def(dst));
        inst.add_operand(MachineOperand::use(src));
        inst.parent_block = block;
        if (!function.insert_instruction_before(block, cursor, inst.id)) {
            throw std::logic_error("DGTL arm64 backend invariant violation: failed to insert physical register copy instruction");
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

export namespace dgtl::isa {
    using arm64::ARM64MachineOpcode;
    using arm64::ARM64TargetInstructionInfo;
}
