/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Component:   Type-Safe Machine Instruction Views (MIR Op Wrappers)
 *
 * Provides concept-constrained, zero-cost semantic views around
 * const MachineInstruction& for target lowering, instruction encoders,
 * peephole optimizers, and machine schedulers.
 */

module;

#include <cstddef>
#include <cstdint>
#include <concepts>
#include <span>
#include <string_view>
#include <type_traits>
#include <vector>

export module dgtl.mir.op_wrappers;

import dgtl.mir.machine_ir;

export namespace dgtl::machine::views {

inline const MachineOperand kInvalidOperand{};

/* ============================================================================
 * Concepts
 * ========================================================================== */

template <typename T>
concept MachineInstructionLike = requires(const T& t) {
    { t.operands } -> std::same_as<const std::vector<MachineOperand>&>;
    { t.form } -> std::same_as<const MachineForm&>;
    { t.opcode } -> std::same_as<const MachineOpcode&>;
    { t.traits } -> std::same_as<const MachineTrait&>;
};

/* ============================================================================
 * MachineBinaryOpView
 * ========================================================================== */

class MachineBinaryOpView {
public:
    explicit constexpr MachineBinaryOpView(const MachineInstruction& inst) noexcept
        : inst_(inst) {}

    [[nodiscard]] const MachineOperand& dst() const noexcept {
        return (inst_.operands.size() > 0) ? inst_.operands[0] : kInvalidOperand;
    }

    [[nodiscard]] const MachineOperand& lhs() const noexcept {
        return (inst_.operands.size() > 1) ? inst_.operands[1] : kInvalidOperand;
    }

    [[nodiscard]] const MachineOperand& rhs() const noexcept {
        return (inst_.operands.size() > 2) ? inst_.operands[2] : kInvalidOperand;
    }

    [[nodiscard]] constexpr MachineForm form() const noexcept {
        return inst_.form;
    }

    [[nodiscard]] constexpr MachineOpcode opcode() const noexcept {
        return inst_.opcode;
    }

    [[nodiscard]] constexpr bool is_generic() const noexcept {
        return is_generic_opcode(inst_.opcode);
    }

    [[nodiscard]] MachineOpcode target_opcode() const noexcept {
        return inst_.target_opcode();
    }

    [[nodiscard]] bool is_target() const noexcept {
        return inst_.has_target_opcode();
    }

    [[nodiscard]] constexpr bool is_commutative() const noexcept {
        return inst_.has_trait(MachineTrait::Commutative);
    }

    [[nodiscard]] constexpr bool has_no_wrap() const noexcept {
        return inst_.has_attribute(MachineAttribute::NoWrap);
    }

    [[nodiscard]] constexpr bool is_exact() const noexcept {
        return inst_.has_attribute(MachineAttribute::Exact);
    }

    [[nodiscard]] constexpr bool is_fast_math() const noexcept {
        return inst_.has_attribute(MachineAttribute::FastMath);
    }

    [[nodiscard]] constexpr std::uint16_t latency_cycles() const noexcept {
        return inst_.latency_cycles;
    }

    [[nodiscard]] constexpr const MachineInstruction& raw_instruction() const noexcept {
        return inst_;
    }

private:
    const MachineInstruction& inst_;
};

/* ============================================================================
 * MachineLoadView
 * ========================================================================== */

class MachineLoadView {
public:
    explicit constexpr MachineLoadView(const MachineInstruction& inst) noexcept
        : inst_(inst) {}

    [[nodiscard]] const MachineOperand& dst() const noexcept {
        return (inst_.operands.size() > 0) ? inst_.operands[0] : kInvalidOperand;
    }

    [[nodiscard]] const MachineOperand& address_operand() const noexcept {
        return (inst_.operands.size() > 1) ? inst_.operands[1] : kInvalidOperand;
    }

    [[nodiscard]] MachineRegister base() const noexcept {
        const auto& op = address_operand();
        if (op.kind == MachineOperandKind::Memory) return op.memory.base;
        if (op.kind == MachineOperandKind::Register) return op.reg;
        return {};
    }

    [[nodiscard]] MachineRegister index() const noexcept {
        const auto& op = address_operand();
        return (op.kind == MachineOperandKind::Memory) ? op.memory.index : MachineRegister{};
    }

    [[nodiscard]] std::int32_t scale() const noexcept {
        const auto& op = address_operand();
        return (op.kind == MachineOperandKind::Memory) ? op.memory.scale : 1;
    }

    [[nodiscard]] std::int64_t displacement() const noexcept {
        const auto& op = address_operand();
        if (op.kind == MachineOperandKind::Memory) return op.memory.displacement;
        if (op.kind == MachineOperandKind::Immediate) return op.immediate;
        return 0;
    }

    [[nodiscard]] std::uint32_t alignment() const noexcept {
        const auto& op = address_operand();
        return (op.kind == MachineOperandKind::Memory) ? op.memory.alignment : 1u;
    }

    [[nodiscard]] StackSlotId stack_slot() const noexcept {
        const auto& op = address_operand();
        return (op.kind == MachineOperandKind::StackSlot) ? op.stack_slot : INVALID_STACK_SLOT;
    }

    [[nodiscard]] constexpr bool is_stack_load() const noexcept {
        return inst_.form == MachineForm::LoadStack || address_operand().kind == MachineOperandKind::StackSlot;
    }

    [[nodiscard]] constexpr bool is_volatile() const noexcept {
        return inst_.has_attribute(MachineAttribute::Volatile) ||
               (address_operand().kind == MachineOperandKind::Memory && address_operand().memory.volatile_access);
    }

    [[nodiscard]] constexpr bool is_atomic() const noexcept {
        return inst_.is_atomic() ||
               (address_operand().kind == MachineOperandKind::Memory && address_operand().memory.atomic);
    }

    [[nodiscard]] constexpr const MachineInstruction& raw_instruction() const noexcept {
        return inst_;
    }

private:
    const MachineInstruction& inst_;
};

/* ============================================================================
 * MachineStoreView
 * ========================================================================== */

class MachineStoreView {
public:
    explicit constexpr MachineStoreView(const MachineInstruction& inst) noexcept
        : inst_(inst) {}

    [[nodiscard]] const MachineOperand& val() const noexcept {
        return (inst_.operands.size() > 0) ? inst_.operands[0] : kInvalidOperand;
    }

    [[nodiscard]] const MachineOperand& address_operand() const noexcept {
        return (inst_.operands.size() > 1) ? inst_.operands[1] : kInvalidOperand;
    }

    [[nodiscard]] MachineRegister base() const noexcept {
        const auto& op = address_operand();
        if (op.kind == MachineOperandKind::Memory) return op.memory.base;
        if (op.kind == MachineOperandKind::Register) return op.reg;
        return {};
    }

    [[nodiscard]] MachineRegister index() const noexcept {
        const auto& op = address_operand();
        return (op.kind == MachineOperandKind::Memory) ? op.memory.index : MachineRegister{};
    }

    [[nodiscard]] std::int32_t scale() const noexcept {
        const auto& op = address_operand();
        return (op.kind == MachineOperandKind::Memory) ? op.memory.scale : 1;
    }

    [[nodiscard]] std::int64_t displacement() const noexcept {
        const auto& op = address_operand();
        if (op.kind == MachineOperandKind::Memory) return op.memory.displacement;
        if (op.kind == MachineOperandKind::Immediate) return op.immediate;
        return 0;
    }

    [[nodiscard]] std::uint32_t alignment() const noexcept {
        const auto& op = address_operand();
        return (op.kind == MachineOperandKind::Memory) ? op.memory.alignment : 1u;
    }

    [[nodiscard]] StackSlotId stack_slot() const noexcept {
        const auto& op = address_operand();
        return (op.kind == MachineOperandKind::StackSlot) ? op.stack_slot : INVALID_STACK_SLOT;
    }

    [[nodiscard]] constexpr bool is_stack_store() const noexcept {
        return inst_.form == MachineForm::StoreStack || address_operand().kind == MachineOperandKind::StackSlot;
    }

    [[nodiscard]] constexpr bool is_volatile() const noexcept {
        return inst_.has_attribute(MachineAttribute::Volatile) ||
               (address_operand().kind == MachineOperandKind::Memory && address_operand().memory.volatile_access);
    }

    [[nodiscard]] constexpr bool is_atomic() const noexcept {
        return inst_.is_atomic() ||
               (address_operand().kind == MachineOperandKind::Memory && address_operand().memory.atomic);
    }

    [[nodiscard]] constexpr const MachineInstruction& raw_instruction() const noexcept {
        return inst_;
    }

private:
    const MachineInstruction& inst_;
};

/* ============================================================================
 * MachineBranchView
 * ========================================================================== */

class MachineBranchView {
public:
    explicit constexpr MachineBranchView(const MachineInstruction& inst) noexcept
        : inst_(inst) {}

    [[nodiscard]] BlockId target_block() const noexcept {
        for (const auto& op : inst_.operands) {
            if (op.kind == MachineOperandKind::BasicBlock) {
                return op.block;
            }
        }
        return INVALID_BLOCK_ID;
    }

    [[nodiscard]] const MachineOperand& condition() const noexcept {
        return (inst_.operands.size() > 0 && inst_.operands[0].kind != MachineOperandKind::BasicBlock)
            ? inst_.operands[0]
            : kInvalidOperand;
    }

    [[nodiscard]] constexpr bool is_conditional() const noexcept {
        return inst_.form == MachineForm::BranchCond || inst_.form == MachineForm::BranchCompare;
    }

    [[nodiscard]] constexpr bool is_fused_compare() const noexcept {
        return inst_.form == MachineForm::BranchCompare;
    }

    [[nodiscard]] constexpr bool is_likely() const noexcept {
        return inst_.has_attribute(MachineAttribute::HotPath);
    }

    [[nodiscard]] constexpr bool is_unlikely() const noexcept {
        return inst_.has_attribute(MachineAttribute::ColdPath);
    }

    [[nodiscard]] constexpr const MachineInstruction& raw_instruction() const noexcept {
        return inst_;
    }

private:
    const MachineInstruction& inst_;
};

/* ============================================================================
 * MachineCallView
 * ========================================================================== */

class MachineCallView {
public:
    explicit constexpr MachineCallView(const MachineInstruction& inst) noexcept
        : inst_(inst) {}

    [[nodiscard]] SymbolId callee_symbol() const noexcept {
        if (!inst_.operands.empty() && inst_.operands[0].kind == MachineOperandKind::Symbol) {
            return inst_.operands[0].symbol;
        }
        return INVALID_SYMBOL;
    }

    [[nodiscard]] MachineRegister callee_reg() const noexcept {
        if (!inst_.operands.empty() && inst_.operands[0].kind == MachineOperandKind::Register) {
            return inst_.operands[0].reg;
        }
        return {};
    }

    [[nodiscard]] std::size_t arg_count() const noexcept {
        return (inst_.operands.size() > 1) ? (inst_.operands.size() - 1) : 0;
    }

    [[nodiscard]] const MachineOperand& arg(std::size_t index) const noexcept {
        return (index + 1 < inst_.operands.size()) ? inst_.operands[index + 1] : kInvalidOperand;
    }

    [[nodiscard]] constexpr bool is_indirect() const noexcept {
        return !inst_.operands.empty() && inst_.operands[0].kind == MachineOperandKind::Register;
    }

    [[nodiscard]] constexpr bool is_syscall() const noexcept {
        return inst_.form == MachineForm::Syscall;
    }

    [[nodiscard]] constexpr bool is_variadic() const noexcept {
        return inst_.has_attribute(MachineAttribute::Variadic);
    }

    [[nodiscard]] constexpr const MachineInstruction& raw_instruction() const noexcept {
        return inst_;
    }

private:
    const MachineInstruction& inst_;
};

/* ============================================================================
 * MachineFmaView
 * ========================================================================== */

class MachineFmaView {
public:
    explicit constexpr MachineFmaView(const MachineInstruction& inst) noexcept
        : inst_(inst) {}

    [[nodiscard]] const MachineOperand& dst() const noexcept {
        return (inst_.operands.size() > 0) ? inst_.operands[0] : kInvalidOperand;
    }

    [[nodiscard]] const MachineOperand& acc() const noexcept {
        return (inst_.operands.size() > 1) ? inst_.operands[1] : kInvalidOperand;
    }

    [[nodiscard]] const MachineOperand& mul_a() const noexcept {
        return (inst_.operands.size() > 2) ? inst_.operands[2] : kInvalidOperand;
    }

    [[nodiscard]] const MachineOperand& mul_b() const noexcept {
        return (inst_.operands.size() > 3) ? inst_.operands[3] : kInvalidOperand;
    }

    [[nodiscard]] constexpr bool is_fast_math() const noexcept {
        return inst_.has_attribute(MachineAttribute::FastMath);
    }

    [[nodiscard]] constexpr const MachineInstruction& raw_instruction() const noexcept {
        return inst_;
    }

private:
    const MachineInstruction& inst_;
};

/* ============================================================================
 * MachineTensorView
 * ========================================================================== */

class MachineTensorView {
public:
    explicit constexpr MachineTensorView(const MachineInstruction& inst) noexcept
        : inst_(inst) {}

    [[nodiscard]] const MachineOperand& output() const noexcept {
        return (inst_.operands.size() > 0) ? inst_.operands[0] : kInvalidOperand;
    }

    [[nodiscard]] const MachineOperand& input_a() const noexcept {
        return (inst_.operands.size() > 1) ? inst_.operands[1] : kInvalidOperand;
    }

    [[nodiscard]] const MachineOperand& input_b() const noexcept {
        return (inst_.operands.size() > 2) ? inst_.operands[2] : kInvalidOperand;
    }

    [[nodiscard]] constexpr std::uint64_t intent_hash() const noexcept {
        return inst_.intent_hash;
    }

    [[nodiscard]] constexpr const MachineInstruction& raw_instruction() const noexcept {
        return inst_;
    }

private:
    const MachineInstruction& inst_;
};

/* ============================================================================
 * MachineAtomicView
 * ========================================================================== */

class MachineAtomicView {
public:
    explicit constexpr MachineAtomicView(const MachineInstruction& inst) noexcept
        : inst_(inst) {}

    [[nodiscard]] const MachineOperand& target_mem() const noexcept {
        return (inst_.operands.size() > 0) ? inst_.operands[0] : kInvalidOperand;
    }

    [[nodiscard]] const MachineOperand& expected() const noexcept {
        return (inst_.operands.size() > 1) ? inst_.operands[1] : kInvalidOperand;
    }

    [[nodiscard]] const MachineOperand& desired() const noexcept {
        return (inst_.operands.size() > 2) ? inst_.operands[2] : kInvalidOperand;
    }

    [[nodiscard]] constexpr bool is_relaxed() const noexcept {
        return inst_.has_attribute(MachineAttribute::Relaxed);
    }

    [[nodiscard]] constexpr bool is_acquire() const noexcept {
        return inst_.has_attribute(MachineAttribute::Acquire);
    }

    [[nodiscard]] constexpr bool is_release() const noexcept {
        return inst_.has_attribute(MachineAttribute::Release);
    }

    [[nodiscard]] constexpr bool is_seq_cst() const noexcept {
        return inst_.has_attribute(MachineAttribute::SeqCst);
    }

    [[nodiscard]] constexpr const MachineInstruction& raw_instruction() const noexcept {
        return inst_;
    }

private:
    const MachineInstruction& inst_;
};

/* ============================================================================
 * Factory Helpers
 * ========================================================================== */

[[nodiscard]] constexpr MachineBinaryOpView as_binary(const MachineInstruction& inst) noexcept {
    return MachineBinaryOpView(inst);
}

[[nodiscard]] constexpr MachineLoadView as_load(const MachineInstruction& inst) noexcept {
    return MachineLoadView(inst);
}

[[nodiscard]] constexpr MachineStoreView as_store(const MachineInstruction& inst) noexcept {
    return MachineStoreView(inst);
}

[[nodiscard]] constexpr MachineBranchView as_branch(const MachineInstruction& inst) noexcept {
    return MachineBranchView(inst);
}

[[nodiscard]] constexpr MachineCallView as_call(const MachineInstruction& inst) noexcept {
    return MachineCallView(inst);
}

[[nodiscard]] constexpr MachineFmaView as_fma(const MachineInstruction& inst) noexcept {
    return MachineFmaView(inst);
}

[[nodiscard]] constexpr MachineTensorView as_tensor(const MachineInstruction& inst) noexcept {
    return MachineTensorView(inst);
}

[[nodiscard]] constexpr MachineAtomicView as_atomic(const MachineInstruction& inst) noexcept {
    return MachineAtomicView(inst);
}

} // namespace dgtl::machine::views

export namespace dgtl::mir::views {
    using namespace dgtl::machine::views;
}
