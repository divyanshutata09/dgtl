/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Component:   ROCm AMDGPU Target ABI and Frame Lowering Contracts
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
export module dgtl.isa.amdgpu.rocm_abi;
import dgtl.mir.machine_ir;
export namespace dgtl::isa::amdgpu {

class ROCmFrameInfo final : public dgtl::machine::TargetFrameInfo {
public:
    [[nodiscard]] std::uint32_t stack_alignment() const noexcept override {
        return 16u; // AMDGPU private memory / flat scratch alignment
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
 * @brief ROCm AMDGPU Calling Convention and Kernel Dispatch ABI.
 * Parameters are placed in SGPR or kernarg memory segments.
 */
class ROCmABI final : public dgtl::machine::TargetABI {
public:
    [[nodiscard]] dgtl::machine::ABICallPlan classify_call(
        std::span<const dgtl::machine::ABIValue> arguments,
        std::optional<dgtl::machine::ABIValue> return_value) const override {

        using namespace dgtl::machine;

        ABICallPlan plan{};
        plan.stack_alignment = 16u;
        plan.shadow_space_bytes = 0u;

        std::uint32_t arg_idx = 0;

        for (const ABIValue& value : arguments) {
            const bool is_vec =
                value.kind == ABIValueKind::Vector ||
                value.preferred_class == RegisterClassKind::Vector;

            const std::uint32_t size = std::max<std::uint32_t>(value.size_bytes, 1u);

            ABIArgLocation location{};
            location.size_bytes = size;
            location.kind = ABIArgLocationKind::Register;
            location.reg = {
                is_vec ? arg_idx : (256u + arg_idx), // VGPR vs SGPR
                RegisterKind::Physical,
                is_vec ? RegisterClassKind::Vector : RegisterClassKind::General
            };
            ++arg_idx;
            plan.arguments.push_back(location);
        }

        plan.stack_bytes = 0u;

        if (return_value.has_value()) {
            const ABIValue& value = *return_value;
            const bool is_vec =
                value.kind == ABIValueKind::Vector ||
                value.preferred_class == RegisterClassKind::Vector;

            plan.return_value = {
                ABIArgLocationKind::Register,
                {
                    is_vec ? 0u : 256u,
                    RegisterKind::Physical,
                    is_vec ? RegisterClassKind::Vector : RegisterClassKind::General
                },
                0,
                value.size_bytes
            };
        }

        return plan;
    }

    [[nodiscard]] bool is_caller_saved(
        dgtl::machine::MachineRegister) const noexcept override {
        return true;
    }

    [[nodiscard]] bool is_callee_saved(
        dgtl::machine::MachineRegister) const noexcept override {
        return false;
    }

    [[nodiscard]] dgtl::machine::MachineRegister
    stack_pointer_register() const noexcept override {
        // SGPR 32 is standard scratch wave offset
        return {256u + 32u, dgtl::machine::RegisterKind::Physical, dgtl::machine::RegisterClassKind::Address};
    }

    [[nodiscard]] dgtl::machine::MachineRegister
    frame_pointer_register() const noexcept override {
        return {256u + 33u, dgtl::machine::RegisterKind::Physical, dgtl::machine::RegisterClassKind::Address};
    }
};

} // namespace dgtl::isa::amdgpu
