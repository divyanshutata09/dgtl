/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

/*
 * DGTL AArch64 target ABI and frame contracts.
 *
 * AAPCS64 register classification is represented here for the common MIR
 * MachineContext. Complex aggregates remain indirect until the frontend emits
 * an explicit ABIValue classification.
 */
module;

#include <algorithm>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>
export module dgtl.isa.arm64.arm64_abi;
import dgtl.mir.machine_ir;
export namespace dgtl::isa::arm64 {

class ARM64FrameInfo final : public dgtl::machine::TargetFrameInfo {
public:
    [[nodiscard]] std::uint32_t stack_alignment() const noexcept override {
        return 16u;
    }

    [[nodiscard]] std::uint32_t
    spill_alignment(dgtl::machine::RegisterClassKind cls) const noexcept override {
        using namespace dgtl::machine;
        switch (cls) {
            case RegisterClassKind::FloatingPoint: return 8u;
            case RegisterClassKind::Vector: return 16u;
            default: return 8u;
        }
    }

    [[nodiscard]] std::uint32_t
    spill_size(dgtl::machine::RegisterClassKind cls) const noexcept override {
        using namespace dgtl::machine;
        switch (cls) {
            case RegisterClassKind::FloatingPoint: return 8u;
            case RegisterClassKind::Vector: return 16u;
            default: return 8u;
        }
    }

    [[nodiscard]] std::uint32_t
    outgoing_argument_alignment() const noexcept override {
        return 8u;
    }
};

class AAPCS64ABI final : public dgtl::machine::TargetABI {
public:
    [[nodiscard]] dgtl::machine::ABICallPlan classify_call(
        std::span<const dgtl::machine::ABIValue> arguments,
        std::optional<dgtl::machine::ABIValue> return_value) const override {

        using namespace dgtl::machine;

        ABICallPlan plan{};
        plan.stack_alignment = 16u;

        std::uint32_t next_gpr = 0;
        std::uint32_t next_fp = 0;
        std::uint32_t stack_offset = 0;

        for (const ABIValue& value : arguments) {
            const bool fp =
                value.kind == ABIValueKind::FloatingPoint ||
                value.kind == ABIValueKind::Vector ||
                value.preferred_class == RegisterClassKind::FloatingPoint ||
                value.preferred_class == RegisterClassKind::Vector;

            const std::uint32_t size =
                std::max<std::uint32_t>(value.size_bytes, 1u);
            const std::uint32_t aligned =
                (size + 7u) & ~7u;

            ABIArgLocation location{};
            location.size_bytes = size;

            if (fp && next_fp < 8u) {
                location.kind = ABIArgLocationKind::Register;
                location.reg = {
                    32u + next_fp,
                    RegisterKind::Physical,
                    value.preferred_class == RegisterClassKind::Vector
                        ? RegisterClassKind::Vector
                        : RegisterClassKind::FloatingPoint
                };
                ++next_fp;
            } else if (!fp && size <= 8u && next_gpr < 8u) {
                location.kind = ABIArgLocationKind::Register;
                location.reg = {
                    next_gpr,
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

        plan.stack_bytes =
            (stack_offset + 15u) & ~15u;

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
                        32u,
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
                        0u,
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

        if (reg.reg_class == RegisterClassKind::General ||
            reg.reg_class == RegisterClassKind::Integer ||
            reg.reg_class == RegisterClassKind::Address) {
            return reg.id <= 18u || reg.id == 30u;
        }

        if (reg.reg_class == RegisterClassKind::FloatingPoint ||
            reg.reg_class == RegisterClassKind::Vector) {
            return (reg.id >= 32u && reg.id <= 39u) ||
                   (reg.id >= 48u && reg.id <= 63u);
        }

        return false;
    }

    [[nodiscard]] bool is_callee_saved(
        dgtl::machine::MachineRegister reg) const noexcept override {

        using namespace dgtl::machine;

        if (!reg.is_physical()) return false;

        if (reg.reg_class == RegisterClassKind::General ||
            reg.reg_class == RegisterClassKind::Integer ||
            reg.reg_class == RegisterClassKind::Address) {
            return reg.id >= 19u && reg.id <= 28u;
        }

        if (reg.reg_class == RegisterClassKind::FloatingPoint ||
            reg.reg_class == RegisterClassKind::Vector) {
            return reg.id >= 40u && reg.id <= 47u;
        }

        return false;
    }

    [[nodiscard]] dgtl::machine::MachineRegister stack_pointer_register() const noexcept override {
        return {
            31u,
            dgtl::machine::RegisterKind::Physical,
            dgtl::machine::RegisterClassKind::Special
        };
    }

    [[nodiscard]] dgtl::machine::MachineRegister frame_pointer_register() const noexcept override {
        return {
            29u,
            dgtl::machine::RegisterKind::Physical,
            dgtl::machine::RegisterClassKind::General
        };
    }
};

} // namespace dgtl::isa::arm64
