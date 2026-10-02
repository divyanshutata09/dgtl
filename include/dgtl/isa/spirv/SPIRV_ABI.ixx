/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Component:   SPIR-V Target Frame & Calling Convention / Interface ABI
 * Standards:   C++26 / Clang 18+ / MSVC 19.40+ / GCC 14+
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */
module;

#include <algorithm>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

export module dgtl.isa.spirv.abi;

import dgtl.mir.machine_ir;

export namespace dgtl::isa::spirv {

class SPIRVFrameInfo final : public dgtl::machine::TargetFrameInfo {
public:
    [[nodiscard]] std::uint32_t stack_alignment() const noexcept override {
        return 16u; // Vulkan / SPIR-V standard alignment for Function/StorageBuffer storage
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
 * @brief SPIR-V Calling Convention and Function Parameter Classification.
 * In SPIR-V, parameters are passed via OpFunctionParameter SSA IDs.
 */
class SPIRVABI final : public dgtl::machine::TargetABI {
public:
    [[nodiscard]] dgtl::machine::ABICallPlan classify_call(
        std::span<const dgtl::machine::ABIValue> arguments,
        std::optional<dgtl::machine::ABIValue> return_value) const override {

        using namespace dgtl::machine;

        ABICallPlan plan{};
        plan.stack_alignment = 16u;
        plan.shadow_space_bytes = 0u;

        std::uint32_t param_idx = 1;

        for (const ABIValue& value : arguments) {
            const bool fp =
                value.kind == ABIValueKind::FloatingPoint ||
                value.kind == ABIValueKind::Vector ||
                value.preferred_class == RegisterClassKind::FloatingPoint ||
                value.preferred_class == RegisterClassKind::Vector;

            const std::uint32_t size = std::max<std::uint32_t>(value.size_bytes, 4u);

            ABIArgLocation location{};
            location.size_bytes = size;
            location.kind = ABIArgLocationKind::Register;
            location.reg = {
                param_idx,
                RegisterKind::Physical,
                fp ? (value.preferred_class == RegisterClassKind::Vector
                          ? RegisterClassKind::Vector
                          : RegisterClassKind::FloatingPoint)
                   : RegisterClassKind::General
            };
            ++param_idx;
            plan.arguments.push_back(location);
        }

        plan.stack_bytes = 0u;

        if (return_value.has_value()) {
            const bool fp =
                return_value->kind == ABIValueKind::FloatingPoint ||
                return_value->kind == ABIValueKind::Vector ||
                return_value->preferred_class == RegisterClassKind::FloatingPoint ||
                return_value->preferred_class == RegisterClassKind::Vector;

            plan.return_value.kind = ABIArgLocationKind::Register;
            plan.return_value.size_bytes = std::max<std::uint32_t>(return_value->size_bytes, 4u);
            plan.return_value.reg = {
                0u,
                RegisterKind::Physical,
                fp ? (return_value->preferred_class == RegisterClassKind::Vector
                          ? RegisterClassKind::Vector
                          : RegisterClassKind::FloatingPoint)
                   : RegisterClassKind::General
            };
        }

        return plan;
    }

    [[nodiscard]] bool
    is_caller_saved(dgtl::machine::MachineRegister reg) const noexcept override {
        (void)reg;
        return true;
    }

    [[nodiscard]] bool
    is_callee_saved(dgtl::machine::MachineRegister reg) const noexcept override {
        (void)reg;
        return false;
    }

    [[nodiscard]] dgtl::machine::MachineRegister
    stack_pointer_register() const noexcept override {
        return {0u, dgtl::machine::RegisterKind::Physical, dgtl::machine::RegisterClassKind::General};
    }

    [[nodiscard]] dgtl::machine::MachineRegister
    frame_pointer_register() const noexcept override {
        return {0u, dgtl::machine::RegisterKind::Physical, dgtl::machine::RegisterClassKind::General};
    }
};

} // namespace dgtl::isa::spirv
