/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Component:   WebAssembly Target ABI and Frame Contracts
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
export module dgtl.isa.wasm.wasm_abi;
import dgtl.mir.machine_ir;
export namespace dgtl::isa::wasm {

class WasmFrameInfo final : public dgtl::machine::TargetFrameInfo {
public:
    [[nodiscard]] std::uint32_t stack_alignment() const noexcept override {
        return 16u; // WebAssembly linear memory stack alignment
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
 * @brief Canonical WebAssembly Function Signature ABI.
 * Parameters are passed via WebAssembly locals rather than hardware stack frames.
 */
class WasmABI final : public dgtl::machine::TargetABI {
public:
    [[nodiscard]] dgtl::machine::ABICallPlan classify_call(
        std::span<const dgtl::machine::ABIValue> arguments,
        std::optional<dgtl::machine::ABIValue> return_value) const override {

        using namespace dgtl::machine;

        ABICallPlan plan{};
        plan.stack_alignment = 16u;
        plan.shadow_space_bytes = 0u;

        std::uint32_t local_idx = 0;

        for (const ABIValue& value : arguments) {
            const bool fp =
                value.kind == ABIValueKind::FloatingPoint ||
                value.kind == ABIValueKind::Vector ||
                value.preferred_class == RegisterClassKind::FloatingPoint ||
                value.preferred_class == RegisterClassKind::Vector;

            const std::uint32_t size = std::max<std::uint32_t>(value.size_bytes, 1u);

            ABIArgLocation location{};
            location.size_bytes = size;
            location.kind = ABIArgLocationKind::Register;
            location.reg = {
                local_idx,
                RegisterKind::Physical,
                fp ? (value.preferred_class == RegisterClassKind::Vector
                          ? RegisterClassKind::Vector
                          : RegisterClassKind::FloatingPoint)
                   : RegisterClassKind::General
            };
            ++local_idx;
            plan.arguments.push_back(location);
        }

        plan.stack_bytes = 0u;

        if (return_value.has_value()) {
            const ABIValue& value = *return_value;
            const bool fp =
                value.kind == ABIValueKind::FloatingPoint ||
                value.kind == ABIValueKind::Vector ||
                value.preferred_class == RegisterClassKind::FloatingPoint ||
                value.preferred_class == RegisterClassKind::Vector;

            plan.return_value = {
                ABIArgLocationKind::Register,
                {
                    0u,
                    RegisterKind::Physical,
                    fp ? (value.preferred_class == RegisterClassKind::Vector
                              ? RegisterClassKind::Vector
                              : RegisterClassKind::FloatingPoint)
                       : RegisterClassKind::General
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
        // Wasm global pointer for linear memory stack (__stack_pointer)
        return {0u, dgtl::machine::RegisterKind::Physical, dgtl::machine::RegisterClassKind::Address};
    }

    [[nodiscard]] dgtl::machine::MachineRegister
    frame_pointer_register() const noexcept override {
        return {0u, dgtl::machine::RegisterKind::Physical, dgtl::machine::RegisterClassKind::Address};
    }
};

} // namespace dgtl::isa::wasm
