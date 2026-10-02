/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Component:   Machine Liveness, Register Allocator, Spiller & Machine Context
 * Standards:   C++26 / Clang 18+ / MSVC 19.40+ / GCC 14+
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

module;

#include <cstdint>
#include <optional>
#include <vector>

export module dgtl.mir.machine_ir:allocation;

import :types;
import :target;
import :function;

export namespace dgtl::machine {

/* ============================================================================
 * Liveness / live intervals
 * ========================================================================== */

struct MachinePosition {
    InstrId instr = INVALID_INSTR_ID;
    std::uint32_t slot = 0;

    friend constexpr bool operator<(const MachinePosition& a,
                                    const MachinePosition& b) noexcept {
        return a.instr < b.instr ||
               (a.instr == b.instr && a.slot < b.slot);
    }

    friend constexpr bool operator==(const MachinePosition&,
                                     const MachinePosition&) noexcept = default;
};

struct MachineLiveRange {
    MachinePosition start = {};
    MachinePosition end = {};
    bool valid = false;

    [[nodiscard]] bool contains(MachinePosition position) const noexcept {
        return valid && !(position < start) && !(end < position);
    }
};

struct MachineLiveInterval {
    MachineRegister reg = {};
    std::vector<MachineLiveRange> ranges;
    std::vector<MachineRegister> uses;
    std::vector<MachineRegister> defs;

    bool spillable = true;
    bool rematerializable = false;
    bool fixed = false;
};

class MachineLiveness {
public:
    virtual ~MachineLiveness() = default;

    [[nodiscard]] virtual const MachineLiveInterval*
    interval(MachineRegister reg) const noexcept = 0;

    [[nodiscard]] virtual bool
    is_live_at(MachineRegister reg,
               MachinePosition position) const noexcept = 0;

    [[nodiscard]] virtual bool
    interferes(MachineRegister lhs,
               MachineRegister rhs) const noexcept = 0;
};

/* ============================================================================
 * Register Allocator & Spiller contracts
 * ========================================================================== */

enum class AllocationResultKind : std::uint8_t {
    Assigned,
    Spilled,
    Rejected
};

struct RegisterAssignment {
    MachineRegister virtual_reg = {};
    MachineRegister physical_reg = {};
    AllocationResultKind result = AllocationResultKind::Rejected;
    StackSlotId spill_slot = INVALID_STACK_SLOT;
};

class MachineRegisterAllocator {
public:
    virtual ~MachineRegisterAllocator() = default;

    virtual bool run() = 0;

    [[nodiscard]] virtual std::optional<MachineRegister>
    assigned_register(MachineRegister virtual_reg) const noexcept = 0;

    [[nodiscard]] virtual std::optional<StackSlotId>
    spill_slot(MachineRegister virtual_reg) const noexcept = 0;
};

struct SpillRequest {
    MachineRegister virtual_reg = {};
    RegisterClassKind reg_class = RegisterClassKind::Invalid;
    std::uint32_t size_bytes = 0;
    std::uint32_t alignment = 1;
};

struct SpillResult {
    bool spilled = false;
    StackSlotId slot = INVALID_STACK_SLOT;
    std::vector<InstrId> inserted_instructions;
};

class MachineSpiller {
public:
    virtual ~MachineSpiller() = default;

    [[nodiscard]] virtual SpillResult
    spill(MachineFunction& function,
          const SpillRequest& request) = 0;

    virtual bool rewrite_for_spill(MachineFunction& function,
                                   MachineRegister virtual_reg,
                                   StackSlotId slot) = 0;
};

/* ============================================================================
 * Unified Machine Compilation Context
 * ========================================================================== */

struct MachineContext {
    const TargetRegisterInfo* register_info = nullptr;
    const TargetInstructionInfo* instruction_info = nullptr;
    const TargetFrameInfo* frame_info = nullptr;
    const TargetABI* abi = nullptr;
    MachineLiveness* liveness = nullptr;
    MachineRegisterAllocator* register_allocator = nullptr;
    MachineSpiller* spiller = nullptr;
    MachineEmitter* emitter = nullptr;

    [[nodiscard]] bool complete_for_allocation() const noexcept {
        return register_info != nullptr &&
               instruction_info != nullptr &&
               frame_info != nullptr &&
               abi != nullptr &&
               liveness != nullptr &&
               register_allocator != nullptr;
    }

    [[nodiscard]] bool complete_for_emission() const noexcept {
        return complete_for_allocation() &&
               emitter != nullptr;
    }
};

} // namespace dgtl::machine