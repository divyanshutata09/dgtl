/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Component:   Target-Aware Runtime Legalization Policy [INDUSTRY GRADE]
 * Standards:   C++26 / Clang 18+ / MSVC 19.40+ / GCC 14+
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

module;

#include <cstdint>

export module dgtl.mir.runtime_policy;

import dgtl.target;
import dgtl.ir_enums;
import dgtl.mir.target_isel;

// Generated target-aware MachineForm classification.
import dgtl.mir.isel_gen;

/**
 * @brief Target-aware legalization policy for DGTL's IR -> ISA boundary.
 *
 * This layer answers one policy question:
 *   "For this exact TargetMachine and IR opcode, is there a direct generated
 *    ISel path, should the operation be lowered through the DGTL runtime, or
 *    is it unsupported?"
 */
export namespace dgtl::mir::runtime_policy {

using dgtl::mir::MachineForm;

/**
 * @brief Backend disposition after target-aware ISel classification.
 */
enum class LegalizationRoute : std::uint8_t {
    DirectISel = 0,
    RuntimeCall,
    Unhandled
};

/**
 * @brief Classify one IR opcode for one concrete compilation target.
 *
 * Precedence is intentional:
 *   1. Target-aware generated ISel
 *   2. Target runtime-backed lowering provenance
 *   3. Fully unsupported
 */
[[nodiscard]] inline LegalizationRoute classify(
    const core::TargetMachine& target,
    ir::OpCode opcode) noexcept
{
    const MachineForm form =
        dgtl::mir::classify_machine_form(target, opcode);

    if (form != MachineForm::Unhandled) [[likely]] {
        return LegalizationRoute::DirectISel;
    }

    if (dgtl::mir::is_runtime_backed(target, opcode)) [[unlikely]] {
        return LegalizationRoute::RuntimeCall;
    }

    return LegalizationRoute::Unhandled;
}

/**
 * @brief True when generated target-specific ISel owns the lowering.
 */
[[nodiscard]] inline bool has_direct_isel_mapping(
    const core::TargetMachine& target,
    ir::OpCode opcode) noexcept
{
    return classify(target, opcode) == LegalizationRoute::DirectISel;
}

/**
 * @brief True when no direct target ISel exists but a runtime fallback exists.
 */
[[nodiscard]] inline bool should_route_unhandled_to_runtime_call(
    const core::TargetMachine& target,
    ir::OpCode opcode) noexcept
{
    return classify(target, opcode) == LegalizationRoute::RuntimeCall;
}

/**
 * @brief True when neither direct ISel nor a runtime fallback exists.
 */
[[nodiscard]] inline bool is_fully_unhandled_opcode(
    const core::TargetMachine& target,
    ir::OpCode opcode) noexcept
{
    return classify(target, opcode) == LegalizationRoute::Unhandled;
}

/**
 * @brief Convenience predicate for callers that need the final disposition.
 */
[[nodiscard]] inline bool is_direct_or_runtime_supported(
    const core::TargetMachine& target,
    ir::OpCode opcode) noexcept
{
    return classify(target, opcode) != LegalizationRoute::Unhandled;
}

/**
 * @brief Return the generated MachineForm for diagnostics and downstream policy.
 */
[[nodiscard]] inline MachineForm machine_form(
    const core::TargetMachine& target,
    ir::OpCode opcode) noexcept
{
    return dgtl::mir::classify_machine_form(target, opcode);
}

} // namespace dgtl::mir::runtime_policy