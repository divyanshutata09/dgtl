/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Component:   X64 Target Instruction Metadata & Legality Contract
 * Standards:   C++26 / Clang 18+ / MSVC 19.40+ / GCC 14+
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */
module;

#include <cstdlib>
#include <cstdint>
#include <stdexcept>

export module dgtl.isa.x64.x64_instruction_info;

import dgtl.mir.machine_ir;

export namespace dgtl::isa::x64 {

// Stable target-opcode identities. These are DGTL's internal X64 machine
// instruction IDs, not raw x86 instruction bytes or encoding prefixes.
enum class X64MachineOpcode : dgtl::machine::MachineOpcode {
    Nop = dgtl::machine::TARGET_OPCODE_START,
    Move,
    MoveImmediate,
    Load,
    Store,
    IAdd32, ISub32, IMul32, IDiv32, IRem32, IAnd32, IOr32, IXor32, IShl32, IShr32,
    IAdd64, ISub64, IMul64, IDiv64, IRem64, IAnd64, IOr64, IXor64, IShl64, IShr64,
    FAdd32, FSub32, FMul32, FDiv32,
    FAdd64, FSub64, FMul64, FDiv64,
    Fma32,
    Compare32,
    Jump, BranchCond, BranchCompare, Call, Return, Syscall,
    VectorGather512, VectorStore512,
    Fence, Atomic, ConcurrentSync, Tensor
};

[[nodiscard]] constexpr dgtl::machine::MachineOpcode
x64_opcode(X64MachineOpcode opcode) noexcept {
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
        case GM_FADD32: case GM_FSUB32: case GM_FMUL32: case GM_FDIV32:
            return MachineForm::FloatBinary32;
        case GM_FADD64: case GM_FSUB64: case GM_FMUL64: case GM_FDIV64:
            return MachineForm::FloatBinary64;
        case GM_FFMA32: case GM_FFMA64: return MachineForm::FloatTernaryFma32;
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
        case GM_BARRIER_SYNC: case GM_SYNC_BARRIER: return MachineForm::ConcurrentSync;
        case GM_TENSOR_OP: case GM_TENSOR_GEMM: case GM_TENSOR_CONV:
            return MachineForm::TensorOp;
        default: return MachineForm::Unhandled;
    }
}

[[nodiscard]] constexpr dgtl::machine::MachineForm
form_for_x64_opcode(dgtl::machine::MachineOpcode opcode) noexcept {
    using namespace dgtl::machine;
    using X = X64MachineOpcode;
    switch (static_cast<X>(opcode)) {
        case X::Nop: return MachineForm::NoOp;
        case X::Move: return MachineForm::Move;
        case X::MoveImmediate: return MachineForm::LoadImmediate;
        case X::Load: return MachineForm::LoadStack;
        case X::Store: return MachineForm::StoreStack;
        case X::IAdd32: case X::ISub32: case X::IMul32: case X::IDiv32: case X::IRem32:
        case X::IAnd32: case X::IOr32: case X::IXor32: case X::IShl32: case X::IShr32:
            return MachineForm::IntegerBinary32;
        case X::IAdd64: case X::ISub64: case X::IMul64: case X::IDiv64: case X::IRem64:
        case X::IAnd64: case X::IOr64: case X::IXor64: case X::IShl64: case X::IShr64:
            return MachineForm::IntegerBinary64;
        case X::FAdd32: case X::FSub32: case X::FMul32: case X::FDiv32:
            return MachineForm::FloatBinary32;
        case X::FAdd64: case X::FSub64: case X::FMul64: case X::FDiv64:
            return MachineForm::FloatBinary64;
        case X::Fma32: return MachineForm::FloatTernaryFma32;
        case X::Compare32: return MachineForm::Compare32;
        case X::Jump: return MachineForm::Goto;
        case X::BranchCond: return MachineForm::BranchCond;
        case X::BranchCompare: return MachineForm::BranchCompare;
        case X::Call: return MachineForm::Call;
        case X::Return: return MachineForm::Return;
        case X::Syscall: return MachineForm::Syscall;
        case X::VectorGather512: return MachineForm::VectorGather512;
        case X::VectorStore512: return MachineForm::VectorStore512;
        case X::Fence: return MachineForm::HardwareFence;
        case X::Atomic: return MachineForm::AtomicOp;
        case X::ConcurrentSync: return MachineForm::ConcurrentSync;
        case X::Tensor: return MachineForm::TensorOp;
        default: return MachineForm::Unhandled;
    }
}

[[nodiscard]] constexpr bool is_x64_opcode_for_form(
    dgtl::machine::MachineOpcode opcode,
    dgtl::machine::MachineForm form) noexcept {
    return dgtl::machine::is_target_opcode(opcode) &&
           form_for_x64_opcode(opcode) == form;
}

class X64TargetInstructionInfo final :
    public dgtl::machine::CommonTargetInstructionInfo {
public:
    [[nodiscard]] dgtl::machine::MachineOpcode
    opcode_for(dgtl::machine::MachineForm form,
               dgtl::machine::MachineSemanticKind semantic_kind) const noexcept override {
        using namespace dgtl::machine;
        using X = X64MachineOpcode;
        const auto op = [](X value) noexcept { return x64_opcode(value); };
        switch (form) {
            case MachineForm::NoOp: return op(X::Nop);
            case MachineForm::Move: return op(X::Move);
            case MachineForm::LoadImmediate: return op(X::MoveImmediate);
            case MachineForm::LoadStack: return op(X::Load);
            case MachineForm::StoreStack: return op(X::Store);
            case MachineForm::IntegerBinary32:
                switch (semantic_kind) {
                    case MachineSemanticKind::IntegerAdd: return op(X::IAdd32);
                    case MachineSemanticKind::IntegerSub: return op(X::ISub32);
                    case MachineSemanticKind::IntegerMul: return op(X::IMul32);
                    case MachineSemanticKind::IntegerDiv: return op(X::IDiv32);
                    case MachineSemanticKind::IntegerMod: return op(X::IRem32);
                    case MachineSemanticKind::IntegerAnd: return op(X::IAnd32);
                    case MachineSemanticKind::IntegerOr: return op(X::IOr32);
                    case MachineSemanticKind::IntegerXor: return op(X::IXor32);
                    case MachineSemanticKind::IntegerShl: return op(X::IShl32);
                    case MachineSemanticKind::IntegerShr: return op(X::IShr32);
                    default: return INVALID_MACHINE_OPCODE;
                }
            case MachineForm::IntegerBinary64:
                switch (semantic_kind) {
                    case MachineSemanticKind::IntegerAdd: return op(X::IAdd64);
                    case MachineSemanticKind::IntegerSub: return op(X::ISub64);
                    case MachineSemanticKind::IntegerMul: return op(X::IMul64);
                    case MachineSemanticKind::IntegerDiv: return op(X::IDiv64);
                    case MachineSemanticKind::IntegerMod: return op(X::IRem64);
                    case MachineSemanticKind::IntegerAnd: return op(X::IAnd64);
                    case MachineSemanticKind::IntegerOr: return op(X::IOr64);
                    case MachineSemanticKind::IntegerXor: return op(X::IXor64);
                    case MachineSemanticKind::IntegerShl: return op(X::IShl64);
                    case MachineSemanticKind::IntegerShr: return op(X::IShr64);
                    default: return INVALID_MACHINE_OPCODE;
                }
            case MachineForm::FloatBinary32:
                switch (semantic_kind) {
                    case MachineSemanticKind::FloatAdd: return op(X::FAdd32);
                    case MachineSemanticKind::FloatSub: return op(X::FSub32);
                    case MachineSemanticKind::FloatMul: return op(X::FMul32);
                    case MachineSemanticKind::FloatDiv: return op(X::FDiv32);
                    default: return INVALID_MACHINE_OPCODE;
                }
            case MachineForm::FloatBinary64:
                switch (semantic_kind) {
                    case MachineSemanticKind::FloatAdd: return op(X::FAdd64);
                    case MachineSemanticKind::FloatSub: return op(X::FSub64);
                    case MachineSemanticKind::FloatMul: return op(X::FMul64);
                    case MachineSemanticKind::FloatDiv: return op(X::FDiv64);
                    default: return INVALID_MACHINE_OPCODE;
                }
            case MachineForm::FloatTernaryFma32: return op(X::Fma32);
            case MachineForm::Compare32:
                return semantic_kind == MachineSemanticKind::Compare ? op(X::Compare32) : INVALID_MACHINE_OPCODE;
            case MachineForm::Goto: return op(X::Jump);
            case MachineForm::BranchCond: return op(X::BranchCond);
            case MachineForm::BranchCompare: return op(X::BranchCompare);
            case MachineForm::Call: return op(X::Call);
            case MachineForm::Return: return op(X::Return);
            case MachineForm::Syscall: return op(X::Syscall);
            case MachineForm::VectorGather512: return op(X::VectorGather512);
            case MachineForm::VectorStore512: return op(X::VectorStore512);
            case MachineForm::HardwareFence: return op(X::Fence);
            case MachineForm::AtomicOp: return op(X::Atomic);
            case MachineForm::ConcurrentSync: return op(X::ConcurrentSync);
            case MachineForm::TensorOp: return op(X::Tensor);
            default: return INVALID_MACHINE_OPCODE;
        }
    }

    [[nodiscard]] dgtl::machine::MachineInstructionInfo
    info(dgtl::machine::MachineOpcode opcode) const noexcept override {
        using namespace dgtl::machine;
        const MachineForm form = is_target_opcode(opcode)
            ? form_for_x64_opcode(opcode)
            : form_for_generic_opcode(opcode);
        return default_form_info(form);
    }

    [[nodiscard]] bool is_legal(dgtl::machine::MachineOpcode opcode) const noexcept override {
        using namespace dgtl::machine;
        if (is_target_opcode(opcode)) {
            return form_for_x64_opcode(opcode) != MachineForm::Unhandled;
        }
        // Preserve the target-info contract for clients querying generic MIR
        // opcodes before concrete target lowering has run.
        return is_generic_opcode(opcode);
    }

    [[nodiscard]] bool is_copy(dgtl::machine::MachineOpcode opcode) const noexcept override {
        return opcode == dgtl::machine::GM_COPY || opcode == x64_opcode(X64MachineOpcode::Move);
    }

    [[nodiscard]] bool is_move(dgtl::machine::MachineOpcode opcode) const noexcept override {
        return is_copy(opcode);
    }

    // Spill/reload builders still create target-independent MIR operations.
    // These APIs intentionally return generic opcodes; the pipeline assigns
    // concrete target opcodes after those instructions are materialized.
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
        MachineInstruction& inst = function.create_instruction(x64_opcode(X64MachineOpcode::Store));
        inst.form = MachineForm::StoreStack;
        inst.semantic_kind = MachineSemanticKind::StoreStack;
        inst.add_flag(MachineInstrFlag::Store);
        inst.add_operand(MachineOperand::stack_ref(slot));
        inst.add_operand(MachineOperand::use(src));
        inst.parent_block = block;
        if (!function.insert_instruction_before(block, cursor, inst.id)) {
            throw std::logic_error("DGTL x64 backend invariant violation: failed to insert stack store instruction");
        }
    }

    void load_reg_from_stack_slot(
        dgtl::machine::MachineFunction& function,
        dgtl::machine::BlockId block,
        dgtl::machine::InstrId cursor,
        dgtl::machine::MachineRegister dst,
        dgtl::machine::StackSlotId slot) const override {
        using namespace dgtl::machine;
        MachineInstruction& inst = function.create_instruction(x64_opcode(X64MachineOpcode::Load));
        inst.form = MachineForm::LoadStack;
        inst.semantic_kind = MachineSemanticKind::LoadStack;
        inst.add_flag(MachineInstrFlag::Load);
        inst.add_operand(MachineOperand::def(dst));
        inst.add_operand(MachineOperand::stack_ref(slot));
        inst.parent_block = block;
        if (!function.insert_instruction_before(block, cursor, inst.id)) {
            throw std::logic_error("DGTL x64 backend invariant violation: failed to insert stack load instruction");
        }
    }

    void copy_phys_reg(
        dgtl::machine::MachineFunction& function,
        dgtl::machine::BlockId block,
        dgtl::machine::InstrId cursor,
        dgtl::machine::MachineRegister dst,
        dgtl::machine::MachineRegister src) const override {
        using namespace dgtl::machine;
        MachineInstruction& inst = function.create_instruction(x64_opcode(X64MachineOpcode::Move));
        inst.form = MachineForm::Move;
        inst.semantic_kind = MachineSemanticKind::Move;
        inst.add_operand(MachineOperand::def(dst));
        inst.add_operand(MachineOperand::use(src));
        inst.parent_block = block;
        if (!function.insert_instruction_before(block, cursor, inst.id)) {
            throw std::logic_error("DGTL x64 backend invariant violation: failed to insert physical register copy instruction");
        }
    }
};

[[nodiscard]] inline dgtl::machine::MachineOpcode
opcode_for(dgtl::machine::MachineForm form,
           dgtl::machine::MachineSemanticKind semantic_kind) noexcept {
    static const X64TargetInstructionInfo info_instance{};
    return info_instance.opcode_for(form, semantic_kind);
}

} // namespace dgtl::isa::x64

export namespace dgtl::isa {
    using x64::X64MachineOpcode;
    using x64::X64TargetInstructionInfo;
}
