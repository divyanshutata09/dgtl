/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Component:   WebAssembly Target Instruction Metadata & Legality Contract
 * Standards:   C++26 / Clang 18+ / MSVC 19.40+ / GCC 14+
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */
module;

#include <cstdlib>
#include <cstdint>
#include <stdexcept>

export module dgtl.isa.wasm.wasm_instruction_info;

import dgtl.mir.machine_ir;

export namespace dgtl::isa::wasm {

// Stable target-opcode identities for WebAssembly (WASM32/SIMD128).
enum class WasmMachineOpcode : dgtl::machine::MachineOpcode {
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
wasm_opcode(WasmMachineOpcode opcode) noexcept {
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
form_for_wasm_opcode(dgtl::machine::MachineOpcode opcode) noexcept {
    using namespace dgtl::machine;
    using W = WasmMachineOpcode;
    switch (static_cast<W>(opcode)) {
        case W::Nop: return MachineForm::NoOp;
        case W::Move: return MachineForm::Move;
        case W::MoveImmediate: return MachineForm::LoadImmediate;
        case W::Load: return MachineForm::LoadStack;
        case W::Store: return MachineForm::StoreStack;
        case W::IAdd32: case W::ISub32: case W::IMul32: case W::IDiv32: case W::IRem32:
        case W::IAnd32: case W::IOr32: case W::IXor32: case W::IShl32: case W::IShr32:
            return MachineForm::IntegerBinary32;
        case W::IAdd64: case W::ISub64: case W::IMul64: case W::IDiv64: case W::IRem64:
        case W::IAnd64: case W::IOr64: case W::IXor64: case W::IShl64: case W::IShr64:
            return MachineForm::IntegerBinary64;
        case W::FAdd32: case W::FSub32: case W::FMul32: case W::FDiv32: case W::FSqrt32:
            return MachineForm::FloatBinary32;
        case W::FAdd64: case W::FSub64: case W::FMul64: case W::FDiv64: case W::FSqrt64:
            return MachineForm::FloatBinary64;
        case W::Fma32: case W::Fma64: return MachineForm::FloatTernaryFma32;
        case W::Compare32: return MachineForm::Compare32;
        case W::Jump: return MachineForm::Goto;
        case W::BranchCond: return MachineForm::BranchCond;
        case W::BranchCompare: return MachineForm::BranchCompare;
        case W::Call: return MachineForm::Call;
        case W::Return: return MachineForm::Return;
        case W::Syscall: return MachineForm::Syscall;
        case W::VectorGather512: return MachineForm::VectorGather512;
        case W::VectorStore512: return MachineForm::VectorStore512;
        case W::Fence: return MachineForm::HardwareFence;
        case W::Atomic: return MachineForm::AtomicOp;
        case W::ConcurrentSync: return MachineForm::ConcurrentSync;
        case W::Tensor: return MachineForm::TensorOp;
        default: return MachineForm::Unhandled;
    }
}

[[nodiscard]] constexpr bool is_wasm_opcode_for_form(
    dgtl::machine::MachineOpcode opcode,
    dgtl::machine::MachineForm form) noexcept {
    return dgtl::machine::is_target_opcode(opcode) &&
           form_for_wasm_opcode(opcode) == form;
}

class WasmTargetInstructionInfo final :
    public dgtl::machine::CommonTargetInstructionInfo {
public:
    [[nodiscard]] dgtl::machine::MachineOpcode
    opcode_for(dgtl::machine::MachineForm form,
               dgtl::machine::MachineSemanticKind semantic_kind) const noexcept override {
        using namespace dgtl::machine;
        using W = WasmMachineOpcode;
        const auto op = [](W value) noexcept { return wasm_opcode(value); };
        switch (form) {
            case MachineForm::NoOp: return op(W::Nop);
            case MachineForm::Move: return op(W::Move);
            case MachineForm::LoadImmediate: return op(W::MoveImmediate);
            case MachineForm::LoadStack: return op(W::Load);
            case MachineForm::StoreStack: return op(W::Store);
            case MachineForm::IntegerBinary32:
                switch (semantic_kind) {
                    case MachineSemanticKind::IntegerAdd: return op(W::IAdd32);
                    case MachineSemanticKind::IntegerSub: return op(W::ISub32);
                    case MachineSemanticKind::IntegerMul: return op(W::IMul32);
                    case MachineSemanticKind::IntegerDiv: return op(W::IDiv32);
                    case MachineSemanticKind::IntegerMod: return op(W::IRem32);
                    case MachineSemanticKind::IntegerAnd: return op(W::IAnd32);
                    case MachineSemanticKind::IntegerOr:  return op(W::IOr32);
                    case MachineSemanticKind::IntegerXor: return op(W::IXor32);
                    case MachineSemanticKind::IntegerShl: return op(W::IShl32);
                    case MachineSemanticKind::IntegerShr: return op(W::IShr32);
                    default: return INVALID_MACHINE_OPCODE;
                }
            case MachineForm::IntegerBinary64:
                switch (semantic_kind) {
                    case MachineSemanticKind::IntegerAdd: return op(W::IAdd64);
                    case MachineSemanticKind::IntegerSub: return op(W::ISub64);
                    case MachineSemanticKind::IntegerMul: return op(W::IMul64);
                    case MachineSemanticKind::IntegerDiv: return op(W::IDiv64);
                    case MachineSemanticKind::IntegerMod: return op(W::IRem64);
                    case MachineSemanticKind::IntegerAnd: return op(W::IAnd64);
                    case MachineSemanticKind::IntegerOr:  return op(W::IOr64);
                    case MachineSemanticKind::IntegerXor: return op(W::IXor64);
                    case MachineSemanticKind::IntegerShl: return op(W::IShl64);
                    case MachineSemanticKind::IntegerShr: return op(W::IShr64);
                    default: return INVALID_MACHINE_OPCODE;
                }
            case MachineForm::FloatBinary32:
                switch (semantic_kind) {
                    case MachineSemanticKind::FloatAdd: return op(W::FAdd32);
                    case MachineSemanticKind::FloatSub: return op(W::FSub32);
                    case MachineSemanticKind::FloatMul: return op(W::FMul32);
                    case MachineSemanticKind::FloatDiv: return op(W::FDiv32);
                    case MachineSemanticKind::FloatSqrt: return op(W::FSqrt32);
                    default: return INVALID_MACHINE_OPCODE;
                }
            case MachineForm::FloatBinary64:
                switch (semantic_kind) {
                    case MachineSemanticKind::FloatAdd: return op(W::FAdd64);
                    case MachineSemanticKind::FloatSub: return op(W::FSub64);
                    case MachineSemanticKind::FloatMul: return op(W::FMul64);
                    case MachineSemanticKind::FloatDiv: return op(W::FDiv64);
                    case MachineSemanticKind::FloatSqrt: return op(W::FSqrt64);
                    default: return INVALID_MACHINE_OPCODE;
                }
            case MachineForm::FloatTernaryFma32: return op(W::Fma32);
            case MachineForm::Compare32:
                return (semantic_kind == MachineSemanticKind::Compare || semantic_kind == MachineSemanticKind::FloatCmp) ? op(W::Compare32) : INVALID_MACHINE_OPCODE;
            case MachineForm::Goto: return op(W::Jump);
            case MachineForm::BranchCond: return op(W::BranchCond);
            case MachineForm::BranchCompare: return op(W::BranchCompare);
            case MachineForm::Call: return op(W::Call);
            case MachineForm::Return: return op(W::Return);
            case MachineForm::Syscall: return op(W::Syscall);
            case MachineForm::VectorGather512: return op(W::VectorGather512);
            case MachineForm::VectorStore512: return op(W::VectorStore512);
            case MachineForm::HardwareFence: return op(W::Fence);
            case MachineForm::AtomicOp: return op(W::Atomic);
            case MachineForm::ConcurrentSync: return op(W::ConcurrentSync);
            case MachineForm::TensorOp: return op(W::Tensor);
            default: return INVALID_MACHINE_OPCODE;
        }
    }

    [[nodiscard]] dgtl::machine::MachineInstructionInfo
    info(dgtl::machine::MachineOpcode opcode) const noexcept override {
        using namespace dgtl::machine;
        const MachineForm form = is_target_opcode(opcode)
            ? form_for_wasm_opcode(opcode)
            : form_for_generic_opcode(opcode);
        return default_form_info(form);
    }

    [[nodiscard]] bool is_legal(dgtl::machine::MachineOpcode opcode) const noexcept override {
        using namespace dgtl::machine;
        if (is_target_opcode(opcode)) {
            return form_for_wasm_opcode(opcode) != MachineForm::Unhandled;
        }
        return is_generic_opcode(opcode);
    }

    [[nodiscard]] bool is_copy(dgtl::machine::MachineOpcode opcode) const noexcept override {
        return opcode == dgtl::machine::GM_COPY || opcode == wasm_opcode(WasmMachineOpcode::Move);
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
        MachineInstruction& inst = function.create_instruction(wasm_opcode(WasmMachineOpcode::Store));
        inst.form = MachineForm::StoreStack;
        inst.semantic_kind = MachineSemanticKind::StoreStack;
        inst.add_flag(MachineInstrFlag::Store);
        inst.add_operand(MachineOperand::stack_ref(slot));
        inst.add_operand(MachineOperand::use(src));
        inst.parent_block = block;
        if (!function.insert_instruction_before(block, cursor, inst.id)) {
            throw std::logic_error("DGTL wasm backend invariant violation: failed to insert stack store instruction");
        }
    }

    void load_reg_from_stack_slot(
        dgtl::machine::MachineFunction& function,
        dgtl::machine::BlockId block,
        dgtl::machine::InstrId cursor,
        dgtl::machine::MachineRegister dst,
        dgtl::machine::StackSlotId slot) const override {
        using namespace dgtl::machine;
        MachineInstruction& inst = function.create_instruction(wasm_opcode(WasmMachineOpcode::Load));
        inst.form = MachineForm::LoadStack;
        inst.semantic_kind = MachineSemanticKind::LoadStack;
        inst.add_flag(MachineInstrFlag::Load);
        inst.add_operand(MachineOperand::def(dst));
        inst.add_operand(MachineOperand::stack_ref(slot));
        inst.parent_block = block;
        if (!function.insert_instruction_before(block, cursor, inst.id)) {
            throw std::logic_error("DGTL wasm backend invariant violation: failed to insert stack load instruction");
        }
    }

    void copy_phys_reg(
        dgtl::machine::MachineFunction& function,
        dgtl::machine::BlockId block,
        dgtl::machine::InstrId cursor,
        dgtl::machine::MachineRegister dst,
        dgtl::machine::MachineRegister src) const override {
        using namespace dgtl::machine;
        MachineInstruction& inst = function.create_instruction(wasm_opcode(WasmMachineOpcode::Move));
        inst.form = MachineForm::Move;
        inst.semantic_kind = MachineSemanticKind::Move;
        inst.add_operand(MachineOperand::def(dst));
        inst.add_operand(MachineOperand::use(src));
        inst.parent_block = block;
        if (!function.insert_instruction_before(block, cursor, inst.id)) {
            throw std::logic_error("DGTL wasm backend invariant violation: failed to insert physical register copy instruction");
        }
    }
};

[[nodiscard]] inline dgtl::machine::MachineOpcode
opcode_for(dgtl::machine::MachineForm form,
           dgtl::machine::MachineSemanticKind semantic_kind) noexcept {
    static const WasmTargetInstructionInfo info_instance{};
    return info_instance.opcode_for(form, semantic_kind);
}

} // namespace dgtl::isa::wasm

export namespace dgtl::isa {
    using wasm::WasmMachineOpcode;
    using wasm::WasmTargetInstructionInfo;
}