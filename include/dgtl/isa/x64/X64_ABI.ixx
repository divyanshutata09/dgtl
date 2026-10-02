/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Component:   X64 Target ABI and Frame Lowering Contracts
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
export module dgtl.isa.x64.x64_abi;
import dgtl.mir.machine_ir;
export namespace dgtl::isa::x64 {

class X64FrameInfo final : public dgtl::machine::TargetFrameInfo {
public:
    [[nodiscard]] std::uint32_t stack_alignment() const noexcept override {
        return 16u;
    }

    [[nodiscard]] std::uint32_t
    spill_alignment(dgtl::machine::RegisterClassKind cls) const noexcept override {
        using namespace dgtl::machine;
        switch (cls) {
            case RegisterClassKind::Vector: return 64u; // AVX-512 alignment
            case RegisterClassKind::FloatingPoint: return 8u;
            default: return 8u;
        }
    }

    [[nodiscard]] std::uint32_t
    spill_size(dgtl::machine::RegisterClassKind cls) const noexcept override {
        using namespace dgtl::machine;
        switch (cls) {
            case RegisterClassKind::Vector: return 64u; // AVX-512 ZMM slot
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
 * @brief Microsoft x64 (Win64) Calling Convention ABI.
 * Mandatory 32-byte shadow store, RCX/RDX/R8/R9 integer and XMM0-XMM3 FP passing.
 */
class Win64ABI final : public dgtl::machine::TargetABI {
public:
    [[nodiscard]] dgtl::machine::ABICallPlan classify_call(
        std::span<const dgtl::machine::ABIValue> arguments,
        std::optional<dgtl::machine::ABIValue> return_value) const override {

        using namespace dgtl::machine;

        ABICallPlan plan{};
        plan.stack_alignment = 16u;
        plan.shadow_space_bytes = 32u; // 32-byte shadow space mandatory for Win64

        // Win64 parameter registers: RCX (1), RDX (2), R8 (8), R9 (9)
        static constexpr std::uint32_t kWin64GprArgs[4] = {1u, 2u, 8u, 9u};

        std::uint32_t arg_index = 0;
        std::uint32_t stack_offset = 32u; // starts after the 32-byte shadow space

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

            if (arg_index < 4u) {
                location.kind = ABIArgLocationKind::Register;
                if (fp) {
                    location.reg = {
                        value.preferred_class == RegisterClassKind::Vector
                            ? (16u + arg_index) // ZMM0-ZMM3
                            : arg_index,        // XMM0-XMM3
                        RegisterKind::Physical,
                        value.preferred_class == RegisterClassKind::Vector
                            ? RegisterClassKind::Vector
                            : RegisterClassKind::FloatingPoint
                    };
                } else {
                    location.reg = {
                        kWin64GprArgs[arg_index],
                        RegisterKind::Physical,
                        value.kind == ABIValueKind::Pointer
                            ? RegisterClassKind::Address
                            : RegisterClassKind::General
                    };
                }
            } else {
                location.kind = ABIArgLocationKind::Stack;
                location.stack_offset = static_cast<std::int32_t>(stack_offset);
                stack_offset += aligned;
            }

            plan.arguments.push_back(location);
            ++arg_index;
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
                        value.preferred_class == RegisterClassKind::Vector ? 16u : 0u,
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
                        0u, // RAX
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
        if (reg.reg_class == RegisterClassKind::FloatingPoint) {
            return reg.id < 6u; // XMM0-XMM5 volatile
        }
        if (reg.reg_class == RegisterClassKind::Vector) {
            return (reg.id >= 16u && reg.id < 22u) || (reg.id >= 32u && reg.id < 48u);
        }
        // RAX=0, RCX=1, RDX=2, R8=8, R9=9, R10=10, R11=11
        return reg.id == 0u || reg.id == 1u || reg.id == 2u ||
               reg.id == 8u || reg.id == 9u || reg.id == 10u || reg.id == 11u;
    }

    [[nodiscard]] bool is_callee_saved(
        dgtl::machine::MachineRegister reg) const noexcept override {
        using namespace dgtl::machine;
        if (!reg.is_physical()) return false;
        if (reg.reg_class == RegisterClassKind::FloatingPoint) {
            return reg.id >= 6u && reg.id < 16u; // XMM6-XMM15 nonvolatile
        }
        if (reg.reg_class == RegisterClassKind::Vector) {
            return reg.id >= 22u && reg.id < 32u; // ZMM6-ZMM15 nonvolatile
        }
        // RBX=3, RBP=5, RSI=6, RDI=7, R12=12, R13=13, R14=14, R15=15
        return reg.id == 3u || reg.id == 5u || reg.id == 6u || reg.id == 7u ||
               reg.id == 12u || reg.id == 13u || reg.id == 14u || reg.id == 15u;
    }

    [[nodiscard]] dgtl::machine::MachineRegister
    stack_pointer_register() const noexcept override {
        return {4u, dgtl::machine::RegisterKind::Physical, dgtl::machine::RegisterClassKind::Address}; // RSP
    }

    [[nodiscard]] dgtl::machine::MachineRegister
    frame_pointer_register() const noexcept override {
        return {5u, dgtl::machine::RegisterKind::Physical, dgtl::machine::RegisterClassKind::Address}; // RBP
    }
};

/**
 * @brief System V AMD64 Calling Convention ABI (Linux, BSD, macOS standard).
 * 6 GPR parameters (RDI, RSI, RDX, RCX, R8, R9), 8 FP parameters (XMM0-XMM7), 0 shadow space.
 */
class SystemVAMD64ABI final : public dgtl::machine::TargetABI {
public:
    [[nodiscard]] dgtl::machine::ABICallPlan classify_call(
        std::span<const dgtl::machine::ABIValue> arguments,
        std::optional<dgtl::machine::ABIValue> return_value) const override {

        using namespace dgtl::machine;

        ABICallPlan plan{};
        plan.stack_alignment = 16u;
        plan.shadow_space_bytes = 0u;

        // System V GPR arguments: RDI (7), RSI (6), RDX (2), RCX (1), R8 (8), R9 (9)
        static constexpr std::uint32_t kSysVGprArgs[6] = {7u, 6u, 2u, 1u, 8u, 9u};

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
                    value.preferred_class == RegisterClassKind::Vector
                        ? (16u + next_fp) // ZMM0-ZMM7
                        : next_fp,        // XMM0-XMM7
                    RegisterKind::Physical,
                    value.preferred_class == RegisterClassKind::Vector
                        ? RegisterClassKind::Vector
                        : RegisterClassKind::FloatingPoint
                };
                ++next_fp;
            } else if (!fp && size <= 8u && next_gpr < 6u) {
                location.kind = ABIArgLocationKind::Register;
                location.reg = {
                    kSysVGprArgs[next_gpr],
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
                        value.preferred_class == RegisterClassKind::Vector ? 16u : 0u,
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
                        0u, // RAX
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
        if (reg.reg_class == RegisterClassKind::FloatingPoint ||
            reg.reg_class == RegisterClassKind::Vector) {
            return true; // All XMM/ZMM are caller-saved in System V
        }
        // System V caller-saved: RAX=0, RDX=2, RCX=1, RSI=6, RDI=7, R8=8, R9=9, R10=10, R11=11
        return reg.id == 0u || reg.id == 1u || reg.id == 2u || reg.id == 6u ||
               reg.id == 7u || reg.id == 8u || reg.id == 9u || reg.id == 10u || reg.id == 11u;
    }

    [[nodiscard]] bool is_callee_saved(
        dgtl::machine::MachineRegister reg) const noexcept override {
        using namespace dgtl::machine;
        if (!reg.is_physical()) return false;
        if (reg.reg_class == RegisterClassKind::FloatingPoint ||
            reg.reg_class == RegisterClassKind::Vector) {
            return false;
        }
        // System V callee-saved: RBX=3, RSP=4, RBP=5, R12=12, R13=13, R14=14, R15=15
        return reg.id == 3u || reg.id == 4u || reg.id == 5u ||
               reg.id == 12u || reg.id == 13u || reg.id == 14u || reg.id == 15u;
    }

    [[nodiscard]] dgtl::machine::MachineRegister
    stack_pointer_register() const noexcept override {
        return {4u, dgtl::machine::RegisterKind::Physical, dgtl::machine::RegisterClassKind::Address}; // RSP
    }

    [[nodiscard]] dgtl::machine::MachineRegister
    frame_pointer_register() const noexcept override {
        return {5u, dgtl::machine::RegisterKind::Physical, dgtl::machine::RegisterClassKind::Address}; // RBP
    }
};

} // namespace dgtl::isa::x64

export namespace dgtl::isa {
    using x64::X64FrameInfo;
    using x64::Win64ABI;
    using x64::SystemVAMD64ABI;
}
