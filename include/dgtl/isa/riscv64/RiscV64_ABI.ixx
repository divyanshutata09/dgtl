/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Component:   RISC-V 64 Target ABI (LP64D) and Frame Lowering Contracts
 * Standards:   C++26 / Clang 18+ / MSVC 19.40+ / GCC 14+
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */
module;

#include <cstdlib>
#include <algorithm>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>
export module dgtl.isa.riscv64.riscv64_abi;
import dgtl.mir.machine_ir;
export namespace dgtl::isa::riscv64 {

class RiscV64FrameInfo final : public dgtl::machine::TargetFrameInfo {
public:
    [[nodiscard]] std::uint32_t stack_alignment() const noexcept override {
        return 16u;
    }

    [[nodiscard]] std::uint32_t
    spill_alignment(dgtl::machine::RegisterClassKind cls) const noexcept override {
        using namespace dgtl::machine;
        switch (cls) {
            case RegisterClassKind::Vector: return 16u;
            case RegisterClassKind::FloatingPoint: return 8u;
            default: return 8u;
        }
    }

    [[nodiscard]] std::uint32_t
    spill_size(dgtl::machine::RegisterClassKind cls) const noexcept override {
        using namespace dgtl::machine;
        switch (cls) {
            case RegisterClassKind::Vector: return 16u;
            case RegisterClassKind::FloatingPoint: return 8u;
            default: return 8u;
        }
    }

    [[nodiscard]] std::uint32_t
    outgoing_argument_alignment() const noexcept override {
        return 8u;
    }
};

/**
 * @brief Standard RISC-V LP64D Calling Convention ABI.
 * 8 GPR arguments (a0-a7 / x10-x17), 8 FP arguments (fa0-fa7 / f10-f17).
 */
class RiscV64LP64DABI final : public dgtl::machine::TargetABI {
public:
    [[nodiscard]] dgtl::machine::ABICallPlan classify_call(
        std::span<const dgtl::machine::ABIValue> arguments,
        std::optional<dgtl::machine::ABIValue> return_value) const override {

        using namespace dgtl::machine;

        ABICallPlan plan{};
        plan.stack_alignment = 16u;
        plan.shadow_space_bytes = 0u;

        std::uint32_t next_gpr = 0;
        std::uint32_t next_fp = 0;
        std::uint32_t stack_offset = 0;

        for (const ABIValue& value : arguments) {
            const bool fp =
                value.kind == ABIValueKind::FloatingPoint ||
                value.kind == ABIValueKind::Vector ||
                value.preferred_class == RegisterClassKind::FloatingPoint ||
                value.preferred_class == RegisterClassKind::Vector;

            const std::uint32_t size = std::max<std::uint32_t>(value.size_bytes, 1u);
            const std::uint32_t aligned = (size + 7u) & ~7u;

            ABIArgLocation location{};
            location.size_bytes = size;

            if (fp && next_fp < 8u) {
                location.kind = ABIArgLocationKind::Register;
                location.reg = {
                    42u + next_fp, // fa0 is f10 (32 + 10 = 42)
                    RegisterKind::Physical,
                    value.preferred_class == RegisterClassKind::Vector
                        ? RegisterClassKind::Vector
                        : RegisterClassKind::FloatingPoint
                };
                ++next_fp;
            } else if (!fp && size <= 8u && next_gpr < 8u) {
                location.kind = ABIArgLocationKind::Register;
                location.reg = {
                    10u + next_gpr, // a0 is x10
                    RegisterKind::Physical,
                    value.kind == ABIValueKind::Pointer
                        ? RegisterClassKind::Address
                        : RegisterClassKind::General
                };
                ++next_gpr;
            } else {
                location.kind = ABIArgLocationKind::Stack;
                location.stack_offset = static_cast<std::int32_t>(stack_offset);
                stack_offset += aligned;
            }

            plan.arguments.push_back(location);
        }

        plan.stack_bytes = (stack_offset + 15u) & ~15u;

        if (return_value.has_value()) {
            const ABIValue& value = *return_value;
            const bool fp =
                value.kind == ABIValueKind::FloatingPoint ||
                value.kind == ABIValueKind::Vector ||
                value.preferred_class == RegisterClassKind::FloatingPoint ||
                value.preferred_class == RegisterClassKind::Vector;

            if (fp) {
                plan.return_value = {
                    ABIArgLocationKind::Register,
                    {
                        42u, // fa0
                        RegisterKind::Physical,
                        value.preferred_class == RegisterClassKind::Vector
                            ? RegisterClassKind::Vector
                            : RegisterClassKind::FloatingPoint
                    },
                    0,
                    value.size_bytes
                };
            } else {
                plan.return_value = {
                    ABIArgLocationKind::Register,
                    {
                        10u, // a0
                        RegisterKind::Physical,
                        value.kind == ABIValueKind::Pointer
                            ? RegisterClassKind::Address
                            : RegisterClassKind::General
                    },
                    0,
                    value.size_bytes
                };
            }
        }

        return plan;
    }

    [[nodiscard]] bool is_caller_saved(
        dgtl::machine::MachineRegister reg) const noexcept override {
        using namespace dgtl::machine;
        if (!reg.is_physical()) return false;
        // GPR caller-saved: ra(1), t0-t2(5-7), a0-a7(10-17), t3-t6(28-31)
        if (reg.id == 1u || (reg.id >= 5u && reg.id <= 7u) ||
            (reg.id >= 10u && reg.id <= 17u) || (reg.id >= 28u && reg.id <= 31u)) {
            return true;
        }
        // FPR caller-saved: ft0-ft7(32-39), fa0-fa7(42-49), ft8-ft11(60-63)
        if ((reg.id >= 32u && reg.id <= 39u) || (reg.id >= 42u && reg.id <= 49u) ||
            (reg.id >= 60u && reg.id <= 63u)) {
            return true;
        }
        if (reg.id >= 64u && reg.id < 96u) return true; // Vector regs
        return false;
    }

    [[nodiscard]] bool is_callee_saved(
        dgtl::machine::MachineRegister reg) const noexcept override {
        using namespace dgtl::machine;
        if (!reg.is_physical()) return false;
        // GPR callee-saved: sp(2), s0/fp(8), s1(9), s2-s11(18-27)
        if (reg.id == 2u || reg.id == 8u || reg.id == 9u || (reg.id >= 18u && reg.id <= 27u)) {
            return true;
        }
        // FPR callee-saved: fs0-fs1(40-41), fs2-fs11(50-59)
        if ((reg.id >= 40u && reg.id <= 41u) || (reg.id >= 50u && reg.id <= 59u)) {
            return true;
        }
        return false;
    }

    [[nodiscard]] dgtl::machine::MachineRegister
    stack_pointer_register() const noexcept override {
        return {2u, dgtl::machine::RegisterKind::Physical, dgtl::machine::RegisterClassKind::Address}; // sp (x2)
    }

    [[nodiscard]] dgtl::machine::MachineRegister
    frame_pointer_register() const noexcept override {
        return {8u, dgtl::machine::RegisterKind::Physical, dgtl::machine::RegisterClassKind::Address}; // s0/fp (x8)
    }
};

} // namespace dgtl::isa::riscv64
