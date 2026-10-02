/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Component:   Target Interface Contracts (Register, Instruction, Frame & ABI)
 * Standards:   C++26 / Clang 18+ / MSVC 19.40+ / GCC 14+
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

module;

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

export module dgtl.mir.machine_ir:target;

import :types;

export namespace dgtl::machine {

class MachineFunction;

class TargetRegisterInfo {
public:
    virtual ~TargetRegisterInfo() = default;

    [[nodiscard]] virtual std::size_t physical_register_count() const noexcept = 0;
    [[nodiscard]] virtual bool is_allocatable(MachineRegister reg) const noexcept = 0;
    [[nodiscard]] virtual RegisterClassKind register_class(MachineRegister reg) const noexcept = 0;
    [[nodiscard]] virtual bool aliases(MachineRegister lhs, MachineRegister rhs) const noexcept = 0;
    [[nodiscard]] virtual bool is_reserved(MachineRegister reg) const noexcept = 0;
    [[nodiscard]] virtual bool is_caller_saved(MachineRegister reg) const noexcept = 0;
    [[nodiscard]] virtual bool is_callee_saved(MachineRegister reg) const noexcept = 0;
};

struct MachineInstructionInfo {
    std::uint16_t required_operand_count = 0;
    std::uint16_t result_count = 0;
    std::uint32_t register_class_mask = 0;

    bool may_load = false;
    bool may_store = false;
    bool is_call = false;
    bool is_return = false;
    bool is_branch = false;
    bool is_terminator = false;
    bool is_pseudo = false;
    bool is_barrier = false;
    bool is_atomic = false;
    bool is_move = false;
    bool commutable = false;

    MachineTrait default_traits = MachineTrait::None;
    MachineDomain default_domains = MachineDomain::None;
    MachineAttribute default_attributes = MachineAttribute::None;
    std::uint16_t default_latency = 1;
};

enum class MachineRegisterConstraintKind : std::uint8_t {
    None,
    FixedRegister,
    RegisterClass
};

struct MachineRegisterConstraint {
    MachineRegisterConstraintKind kind = MachineRegisterConstraintKind::None;
    MachineRegister fixed_register = {};
    RegisterClassKind required_class = RegisterClassKind::Invalid;

    [[nodiscard]] static constexpr MachineRegisterConstraint none() noexcept {
        return {};
    }

    [[nodiscard]] static constexpr MachineRegisterConstraint fixed(
        MachineRegister reg) noexcept {
        return {
            MachineRegisterConstraintKind::FixedRegister,
            reg,
            reg.reg_class
        };
    }

    [[nodiscard]] static constexpr MachineRegisterConstraint
    register_class(RegisterClassKind cls) noexcept {
        return {
            MachineRegisterConstraintKind::RegisterClass,
            {},
            cls
        };
    }
};

class TargetInstructionInfo {
public:
    virtual ~TargetInstructionInfo() = default;

    [[nodiscard]] virtual MachineOpcode
    opcode_for(MachineForm form,
               MachineSemanticKind semantic_kind) const noexcept {
        (void)form;
        (void)semantic_kind;
        return INVALID_MACHINE_OPCODE;
    }

    [[nodiscard]] virtual MachineInstructionInfo
    info(MachineOpcode opcode) const noexcept = 0;

    [[nodiscard]] virtual bool
    is_legal(MachineOpcode opcode) const noexcept = 0;

    [[nodiscard]] virtual bool
    is_copy(MachineOpcode opcode) const noexcept = 0;

    [[nodiscard]] virtual bool
    is_move(MachineOpcode opcode) const noexcept = 0;

    [[nodiscard]] virtual MachineRegisterConstraint
    register_constraint(MachineOpcode opcode,
                        std::size_t operand_index) const noexcept {
        (void)opcode;
        (void)operand_index;
        return MachineRegisterConstraint::none();
    }

    [[nodiscard]] virtual MachineOpcode
    load_opcode(RegisterClassKind reg_class) const noexcept {
        (void)reg_class;
        return INVALID_MACHINE_OPCODE;
    }

    [[nodiscard]] virtual MachineOpcode
    store_opcode(RegisterClassKind reg_class) const noexcept {
        (void)reg_class;
        return INVALID_MACHINE_OPCODE;
    }
};

class CommonTargetInstructionInfo : public TargetInstructionInfo {
public:
    [[nodiscard]] static constexpr MachineInstructionInfo default_form_info(MachineForm form) noexcept {
        MachineInstructionInfo result{};
        switch (form) {
            case MachineForm::LoadStack:
                result.may_load = true;
                result.required_operand_count = 2;
                result.result_count = 1;
                result.default_domains = MachineDomain::Memory;
                result.default_traits = MachineTrait::IsMemoryRead | MachineTrait::IsMemoryOp;
                result.default_latency = 3;
                break;
            case MachineForm::StoreStack:
                result.may_store = true;
                result.required_operand_count = 2;
                result.default_domains = MachineDomain::Memory;
                result.default_traits = MachineTrait::IsMemoryWrite | MachineTrait::IsMemoryOp;
                result.default_latency = 1;
                break;
            case MachineForm::Call:
                result.is_call = true;
                result.required_operand_count = 1;
                result.default_domains = MachineDomain::ControlFlow;
                result.default_latency = 10;
                break;
            case MachineForm::Return:
                result.is_return = true;
                result.is_terminator = true;
                result.default_domains = MachineDomain::ControlFlow | MachineDomain::Terminator;
                result.default_traits = MachineTrait::IsTerminator;
                result.default_latency = 1;
                break;
            case MachineForm::Goto:
            case MachineForm::BranchCond:
            case MachineForm::BranchCompare:
                result.is_branch = true;
                result.is_terminator = true;
                result.default_domains = MachineDomain::ControlFlow | MachineDomain::Terminator;
                result.default_traits = MachineTrait::IsTerminator;
                result.default_latency = 1;
                break;
            case MachineForm::HardwareFence:
                result.is_barrier = true;
                result.default_domains = MachineDomain::Concurrency;
                result.default_traits = MachineTrait::MemoryBarrier;
                result.default_latency = 5;
                break;
            case MachineForm::AtomicOp:
                result.is_atomic = true;
                result.default_domains = MachineDomain::Concurrency | MachineDomain::Memory;
                result.default_traits = MachineTrait::IsMemoryOp;
                result.default_latency = 12;
                break;
            case MachineForm::Move:
                result.is_move = true;
                result.required_operand_count = 2;
                result.result_count = 1;
                result.default_domains = MachineDomain::Arithmetic;
                result.default_traits = MachineTrait::HasNoSideEffects;
                result.default_latency = 1;
                break;
            case MachineForm::LoadImmediate:
                result.required_operand_count = 2;
                result.result_count = 1;
                result.default_domains = MachineDomain::Arithmetic;
                result.default_traits = MachineTrait::ConstantFoldable | MachineTrait::HasNoSideEffects;
                result.default_latency = 1;
                break;
            case MachineForm::IntegerBinary32:
            case MachineForm::IntegerBinary64:
                result.required_operand_count = 3;
                result.result_count = 1;
                result.default_domains = MachineDomain::Arithmetic;
                result.default_traits = MachineTrait::HasNoSideEffects;
                result.default_latency = 1;
                break;
            case MachineForm::FloatBinary32:
            case MachineForm::FloatBinary64:
                result.required_operand_count = 3;
                result.result_count = 1;
                result.default_domains = MachineDomain::Arithmetic;
                result.default_traits = MachineTrait::HasNoSideEffects;
                result.default_latency = 3;
                break;
            case MachineForm::Compare32:
                result.required_operand_count = 3;
                result.result_count = 1;
                result.default_domains = MachineDomain::Comparison;
                result.default_traits = MachineTrait::HasNoSideEffects;
                result.default_latency = 1;
                break;
            case MachineForm::FloatTernaryFma32:
                result.required_operand_count = 4;
                result.result_count = 1;
                result.default_domains = MachineDomain::Arithmetic;
                result.default_traits = MachineTrait::IsFusible | MachineTrait::HasNoSideEffects;
                result.default_latency = 4;
                break;
            case MachineForm::VectorBinary512:
                result.required_operand_count = 3;
                result.result_count = 1;
                result.default_domains = MachineDomain::SIMD;
                result.default_traits = MachineTrait::IsVectorizable | MachineTrait::HasNoSideEffects;
                result.default_latency = 3;
                break;
            case MachineForm::VectorGather512:
                result.may_load = true;
                result.required_operand_count = 3;
                result.result_count = 1;
                result.default_domains = MachineDomain::SIMD | MachineDomain::Memory;
                result.default_traits = MachineTrait::IsMemoryRead | MachineTrait::IsVectorizable;
                result.default_latency = 8;
                break;
            case MachineForm::VectorStore512:
                result.may_store = true;
                result.required_operand_count = 3;
                result.default_domains = MachineDomain::SIMD | MachineDomain::Memory;
                result.default_traits = MachineTrait::IsMemoryWrite | MachineTrait::IsVectorizable;
                result.default_latency = 2;
                break;
            case MachineForm::Syscall:
                result.is_call = true;
                result.is_barrier = true;
                result.default_domains = MachineDomain::ControlFlow;
                result.default_traits = MachineTrait::MemoryBarrier;
                result.default_latency = 50;
                break;
            case MachineForm::TensorOp:
                result.required_operand_count = 3;
                result.result_count = 1;
                result.default_domains = MachineDomain::Tensor;
                result.default_traits = MachineTrait::IsTensor;
                result.default_latency = 16;
                break;
            case MachineForm::ConcurrentSync:
                result.is_barrier = true;
                result.default_domains = MachineDomain::Concurrency;
                result.default_traits = MachineTrait::MemoryBarrier;
                result.default_latency = 10;
                break;
            case MachineForm::NoOp:
            case MachineForm::Unhandled:
            default:
                break;
        }
        return result;
    }

    [[nodiscard]] bool is_legal(MachineOpcode opcode) const noexcept override {
        return opcode != INVALID_MACHINE_OPCODE;
    }

    [[nodiscard]] bool is_copy(MachineOpcode opcode) const noexcept override {
        return opcode == GM_COPY;
    }

    [[nodiscard]] bool is_move(MachineOpcode opcode) const noexcept override {
        return opcode == GM_COPY;
    }
};

class TargetFrameInfo {
public:
    virtual ~TargetFrameInfo() = default;

    [[nodiscard]] virtual std::uint32_t stack_alignment() const noexcept = 0;
    [[nodiscard]] virtual std::uint32_t spill_alignment(RegisterClassKind) const noexcept = 0;
    [[nodiscard]] virtual std::uint32_t spill_size(RegisterClassKind) const noexcept = 0;
    [[nodiscard]] virtual std::uint32_t outgoing_argument_alignment() const noexcept = 0;
};

struct MachineFrameLayout {
    std::uint32_t local_bytes = 0;
    std::uint32_t spill_bytes = 0;
    std::uint32_t callee_saved_bytes = 0;
    std::uint32_t outgoing_args_bytes = 0;
    std::uint32_t alignment_padding = 0;
    std::uint32_t total_frame_bytes = 0;
    std::vector<MachineRegister> saved_registers;
};

class CommonTargetFrameInfo : public TargetFrameInfo {
public:
    [[nodiscard]] std::uint32_t stack_alignment() const noexcept override {
        return 16;
    }

    [[nodiscard]] std::uint32_t spill_alignment(RegisterClassKind cls) const noexcept override {
        switch (cls) {
            case RegisterClassKind::Vector: return 16;
            case RegisterClassKind::FloatingPoint: return 8;
            default: return 8;
        }
    }

    [[nodiscard]] std::uint32_t spill_size(RegisterClassKind cls) const noexcept override {
        switch (cls) {
            case RegisterClassKind::Vector: return 16;
            case RegisterClassKind::FloatingPoint: return 8;
            default: return 8;
        }
    }

    [[nodiscard]] std::uint32_t outgoing_argument_alignment() const noexcept override {
        return 8;
    }

    [[nodiscard]] MachineFrameLayout compute_layout(
        std::uint32_t local_bytes,
        std::uint32_t spill_bytes,
        std::uint32_t outgoing_bytes,
        std::span<const MachineRegister> clobbered_registers) const noexcept {
        MachineFrameLayout layout{};
        layout.local_bytes = local_bytes;
        layout.spill_bytes = spill_bytes;
        layout.outgoing_args_bytes = outgoing_bytes;

        for (const auto& reg : clobbered_registers) {
            layout.saved_registers.push_back(reg);
        }
        layout.callee_saved_bytes = static_cast<std::uint32_t>(layout.saved_registers.size() * 8);

        const std::uint32_t raw_sum = layout.local_bytes + layout.spill_bytes +
                                      layout.callee_saved_bytes + layout.outgoing_args_bytes;
        const std::uint32_t align = stack_alignment();
        if (align > 1) {
            const std::uint32_t rem = raw_sum % align;
            if (rem != 0) {
                layout.alignment_padding = align - rem;
            }
        }
        layout.total_frame_bytes = raw_sum + layout.alignment_padding;
        return layout;
    }
};

/* ============================================================================
 * ABI contract
 * ========================================================================== */

enum class ABIValueKind : std::uint8_t {
    Integer,
    FloatingPoint,
    Vector,
    Aggregate,
    Pointer,
    Predicate,
    Memory
};

struct ABIValue {
    ABIValueKind kind = ABIValueKind::Integer;
    std::uint32_t size_bytes = 0;
    std::uint32_t alignment = 1;
    RegisterClassKind preferred_class = RegisterClassKind::Invalid;
};

enum class ABIArgLocationKind : std::uint8_t {
    Invalid,
    Register,
    Stack,
    Indirect
};

struct ABIArgLocation {
    ABIArgLocationKind kind = ABIArgLocationKind::Invalid;
    MachineRegister reg = {};
    std::int32_t stack_offset = 0;
    std::uint32_t size_bytes = 0;
};

struct ABICallPlan {
    std::vector<ABIArgLocation> arguments;
    ABIArgLocation return_value = {};
    std::uint32_t stack_bytes = 0;
    std::uint32_t stack_alignment = 1;
    std::uint32_t shadow_space_bytes = 0;
};

class TargetABI {
public:
    virtual ~TargetABI() = default;

    [[nodiscard]] virtual ABICallPlan
    classify_call(std::span<const ABIValue> arguments,
                  std::optional<ABIValue> return_value) const = 0;

    [[nodiscard]] virtual bool
    is_caller_saved(MachineRegister reg) const noexcept = 0;

    [[nodiscard]] virtual bool
    is_callee_saved(MachineRegister reg) const noexcept = 0;

    [[nodiscard]] virtual MachineRegister
    stack_pointer_register() const noexcept = 0;

    [[nodiscard]] virtual MachineRegister
    frame_pointer_register() const noexcept = 0;

    [[nodiscard]] virtual bool
    lower_function(MachineFunction& function) const {
        (void)function;
        return false;
    }
};

class MachineCodeEmitter {
public:
    virtual ~MachineCodeEmitter() = default;

    virtual void emit_instruction(const MachineInstruction& inst,
                                  std::vector<std::uint8_t>& out) = 0;
};

struct TargetSubtarget {
    const TargetRegisterInfo& reg_info;
    const TargetInstructionInfo& instr_info;
    const TargetFrameInfo& frame_info;
    const TargetABI& abi;
    const MachineCodeEmitter* emitter = nullptr;
};

} // namespace dgtl::machine