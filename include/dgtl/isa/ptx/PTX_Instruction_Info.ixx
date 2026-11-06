/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Component:   CUDA PTX Target Instruction Metadata & Legality Contract
 * Standards:   C++26 / Clang 18+ / MSVC 19.40+ / GCC 14+
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */
module;

#include <cstdlib>
#include <cstdint>
#include <stdexcept>

export module dgtl.isa.ptx.cuda_instruction_info;

import dgtl.mir.machine_ir;

export namespace dgtl::isa::ptx {

// Stable target-opcode identities for CUDA PTX.
enum class CUDAMachineOpcode : dgtl::machine::MachineOpcode {
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

using PTXMachineOpcode = CUDAMachineOpcode;

[[nodiscard]] constexpr dgtl::machine::MachineOpcode
cuda_opcode(CUDAMachineOpcode opcode) noexcept {
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
form_for_cuda_opcode(dgtl::machine::MachineOpcode opcode) noexcept {
    using namespace dgtl::machine;
    using C = CUDAMachineOpcode;
    switch (static_cast<C>(opcode)) {
        case C::Nop: return MachineForm::NoOp;
        case C::Move: return MachineForm::Move;
        case C::MoveImmediate: return MachineForm::LoadImmediate;
        case C::Load: return MachineForm::LoadStack;
        case C::Store: return MachineForm::StoreStack;
        case C::IAdd32: case C::ISub32: case C::IMul32: case C::IDiv32: case C::IRem32:
        case C::IAnd32: case C::IOr32: case C::IXor32: case C::IShl32: case C::IShr32:
            return MachineForm::IntegerBinary32;
        case C::IAdd64: case C::ISub64: case C::IMul64: case C::IDiv64: case C::IRem64:
        case C::IAnd64: case C::IOr64: case C::IXor64: case C::IShl64: case C::IShr64:
            return MachineForm::IntegerBinary64;
        case C::FAdd32: case C::FSub32: case C::FMul32: case C::FDiv32: case C::FSqrt32:
            return MachineForm::FloatBinary32;
        case C::FAdd64: case C::FSub64: case C::FMul64: case C::FDiv64: case C::FSqrt64:
            return MachineForm::FloatBinary64;
        case C::Fma32: case C::Fma64: return MachineForm::FloatTernaryFma32;
        case C::Compare32: return MachineForm::Compare32;
        case C::Jump: return MachineForm::Goto;
        case C::BranchCond: return MachineForm::BranchCond;
        case C::BranchCompare: return MachineForm::BranchCompare;
        case C::Call: return MachineForm::Call;
        case C::Return: return MachineForm::Return;
        case C::Syscall: return MachineForm::Syscall;
        case C::VectorGather512: return MachineForm::VectorGather512;
        case C::VectorStore512: return MachineForm::VectorStore512;
        case C::Fence: return MachineForm::HardwareFence;
        case C::Atomic: return MachineForm::AtomicOp;
        case C::ConcurrentSync: return MachineForm::ConcurrentSync;
        case C::Tensor: return MachineForm::TensorOp;
        default: return MachineForm::Unhandled;
    }
}

[[nodiscard]] constexpr bool is_cuda_opcode_for_form(
    dgtl::machine::MachineOpcode opcode,
    dgtl::machine::MachineForm form) noexcept {
    return dgtl::machine::is_target_opcode(opcode) &&
           form_for_cuda_opcode(opcode) == form;
}

class CUDATargetInstructionInfo final :
    public dgtl::machine::CommonTargetInstructionInfo {
public:
    [[nodiscard]] dgtl::machine::MachineOpcode
    opcode_for(dgtl::machine::MachineForm form,
               dgtl::machine::MachineSemanticKind semantic_kind) const noexcept override {
        using namespace dgtl::machine;
        using C = CUDAMachineOpcode;
        const auto op = [](C value) noexcept { return cuda_opcode(value); };
        switch (form) {
            case MachineForm::NoOp: return op(C::Nop);
            case MachineForm::Move: return op(C::Move);
            case MachineForm::LoadImmediate: return op(C::MoveImmediate);
            case MachineForm::LoadStack: return op(C::Load);
            case MachineForm::StoreStack: return op(C::Store);
            case MachineForm::IntegerBinary32:
                switch (semantic_kind) {
                    case MachineSemanticKind::IntegerAdd: return op(C::IAdd32);
                    case MachineSemanticKind::IntegerSub: return op(C::ISub32);
                    case MachineSemanticKind::IntegerMul: return op(C::IMul32);
                    case MachineSemanticKind::IntegerDiv: return op(C::IDiv32);
                    case MachineSemanticKind::IntegerMod: return op(C::IRem32);
                    case MachineSemanticKind::IntegerAnd: return op(C::IAnd32);
                    case MachineSemanticKind::IntegerOr:  return op(C::IOr32);
                    case MachineSemanticKind::IntegerXor: return op(C::IXor32);
                    case MachineSemanticKind::IntegerShl: return op(C::IShl32);
                    case MachineSemanticKind::IntegerShr: return op(C::IShr32);
                    default: return INVALID_MACHINE_OPCODE;
                }
            case MachineForm::IntegerBinary64:
                switch (semantic_kind) {
                    case MachineSemanticKind::IntegerAdd: return op(C::IAdd64);
                    case MachineSemanticKind::IntegerSub: return op(C::ISub64);
                    case MachineSemanticKind::IntegerMul: return op(C::IMul64);
                    case MachineSemanticKind::IntegerDiv: return op(C::IDiv64);
                    case MachineSemanticKind::IntegerMod: return op(C::IRem64);
                    case MachineSemanticKind::IntegerAnd: return op(C::IAnd64);
                    case MachineSemanticKind::IntegerOr:  return op(C::IOr64);
                    case MachineSemanticKind::IntegerXor: return op(C::IXor64);
                    case MachineSemanticKind::IntegerShl: return op(C::IShl64);
                    case MachineSemanticKind::IntegerShr: return op(C::IShr64);
                    default: return INVALID_MACHINE_OPCODE;
                }
            case MachineForm::FloatBinary32:
                switch (semantic_kind) {
                    case MachineSemanticKind::FloatAdd: return op(C::FAdd32);
                    case MachineSemanticKind::FloatSub: return op(C::FSub32);
                    case MachineSemanticKind::FloatMul: return op(C::FMul32);
                    case MachineSemanticKind::FloatDiv: return op(C::FDiv32);
                    case MachineSemanticKind::FloatSqrt: return op(C::FSqrt32);
                    default: return INVALID_MACHINE_OPCODE;
                }
            case MachineForm::FloatBinary64:
                switch (semantic_kind) {
                    case MachineSemanticKind::FloatAdd: return op(C::FAdd64);
                    case MachineSemanticKind::FloatSub: return op(C::FSub64);
                    case MachineSemanticKind::FloatMul: return op(C::FMul64);
                    case MachineSemanticKind::FloatDiv: return op(C::FDiv64);
                    case MachineSemanticKind::FloatSqrt: return op(C::FSqrt64);
                    default: return INVALID_MACHINE_OPCODE;
                }
            case MachineForm::FloatTernaryFma32: return op(C::Fma32);
            case MachineForm::Compare32:
                return (semantic_kind == MachineSemanticKind::Compare || semantic_kind == MachineSemanticKind::FloatCmp) ? op(C::Compare32) : INVALID_MACHINE_OPCODE;
            case MachineForm::Goto: return op(C::Jump);
            case MachineForm::BranchCond: return op(C::BranchCond);
            case MachineForm::BranchCompare: return op(C::BranchCompare);
            case MachineForm::Call: return op(C::Call);
            case MachineForm::Return: return op(C::Return);
            case MachineForm::Syscall: return op(C::Syscall);
            case MachineForm::VectorGather512: return op(C::VectorGather512);
            case MachineForm::VectorStore512: return op(C::VectorStore512);
            case MachineForm::HardwareFence: return op(C::Fence);
            case MachineForm::AtomicOp: return op(C::Atomic);
            case MachineForm::ConcurrentSync: return op(C::ConcurrentSync);
            case MachineForm::TensorOp: return op(C::Tensor);
            default: return INVALID_MACHINE_OPCODE;
        }
    }

    [[nodiscard]] dgtl::machine::MachineInstructionInfo
    info(dgtl::machine::MachineOpcode opcode) const noexcept override {
        using namespace dgtl::machine;
        const MachineForm form = is_target_opcode(opcode)
            ? form_for_cuda_opcode(opcode)
            : form_for_generic_opcode(opcode);
        return default_form_info(form);
    }

    [[nodiscard]] bool is_legal(dgtl::machine::MachineOpcode opcode) const noexcept override {
        using namespace dgtl::machine;
        if (is_target_opcode(opcode)) {
            return form_for_cuda_opcode(opcode) != MachineForm::Unhandled;
        }
        return is_generic_opcode(opcode);
    }

    [[nodiscard]] bool is_copy(dgtl::machine::MachineOpcode opcode) const noexcept override {
        return opcode == dgtl::machine::GM_COPY || opcode == cuda_opcode(CUDAMachineOpcode::Move);
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
        MachineInstruction& inst = function.create_instruction(cuda_opcode(CUDAMachineOpcode::Store));
        inst.form = MachineForm::StoreStack;
        inst.semantic_kind = MachineSemanticKind::StoreStack;
        inst.add_flag(MachineInstrFlag::Store);
        inst.add_operand(MachineOperand::stack_ref(slot));
        inst.add_operand(MachineOperand::use(src));
        inst.parent_block = block;
        if (!function.insert_instruction_before(block, cursor, inst.id)) {
            throw std::logic_error("DGTL ptx backend invariant violation: failed to insert stack store instruction");
        }
    }

    void load_reg_from_stack_slot(
        dgtl::machine::MachineFunction& function,
        dgtl::machine::BlockId block,
        dgtl::machine::InstrId cursor,
        dgtl::machine::MachineRegister dst,
        dgtl::machine::StackSlotId slot) const override {
        using namespace dgtl::machine;
        MachineInstruction& inst = function.create_instruction(cuda_opcode(CUDAMachineOpcode::Load));
        inst.form = MachineForm::LoadStack;
        inst.semantic_kind = MachineSemanticKind::LoadStack;
        inst.add_flag(MachineInstrFlag::Load);
        inst.add_operand(MachineOperand::def(dst));
        inst.add_operand(MachineOperand::stack_ref(slot));
        inst.parent_block = block;
        if (!function.insert_instruction_before(block, cursor, inst.id)) {
            throw std::logic_error("DGTL ptx backend invariant violation: failed to insert stack load instruction");
        }
    }

    void copy_phys_reg(
        dgtl::machine::MachineFunction& function,
        dgtl::machine::BlockId block,
        dgtl::machine::InstrId cursor,
        dgtl::machine::MachineRegister dst,
        dgtl::machine::MachineRegister src) const override {
        using namespace dgtl::machine;
        MachineInstruction& inst = function.create_instruction(cuda_opcode(CUDAMachineOpcode::Move));
        inst.form = MachineForm::Move;
        inst.semantic_kind = MachineSemanticKind::Move;
        inst.add_operand(MachineOperand::def(dst));
        inst.add_operand(MachineOperand::use(src));
        inst.parent_block = block;
        if (!function.insert_instruction_before(block, cursor, inst.id)) {
            throw std::logic_error("DGTL ptx backend invariant violation: failed to insert physical register copy instruction");
        }
    }
};

using PTXTargetInstructionInfo = CUDATargetInstructionInfo;

[[nodiscard]] inline dgtl::machine::MachineOpcode
opcode_for(dgtl::machine::MachineForm form,
           dgtl::machine::MachineSemanticKind semantic_kind) noexcept {
    static const CUDATargetInstructionInfo info_instance{};
    return info_instance.opcode_for(form, semantic_kind);
}

} // namespace dgtl::isa::ptx

export namespace dgtl::isa {
    using ptx::CUDAMachineOpcode;
    using ptx::PTXMachineOpcode;
    using ptx::CUDATargetInstructionInfo;
    using ptx::PTXTargetInstructionInfo;
}
