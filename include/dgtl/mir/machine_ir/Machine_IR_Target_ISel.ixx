/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Component:   Target ISel Interface (Pure MIR Target Abstraction)
 * Standards:   C++26 / Clang 18+ / MSVC 19.40+ / GCC 14+
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

module;

#include <span>
#include <string_view>
#include <cstdint>

export module dgtl.mir.target_isel;

import dgtl.target;
import dgtl.ir_enums;
import dgtl.ir_node;
import dgtl.ir.stream;
import dgtl.mir.isel_gen;

export namespace dgtl::mir {

using MachineForm = dgtl::mir::MachineForm;
using MachineSemanticKind = dgtl::mir::MachineSemanticKind;

class TargetISel {
public:
    virtual ~TargetISel() = default;

    [[nodiscard]] virtual MachineForm classify_machine_form(ir::OpCode opcode) const noexcept = 0;
    [[nodiscard]] virtual MachineForm classify_machine_node(const ir::IRNode& node) const noexcept {
        return classify_machine_form(node.opcode);
    }
    [[nodiscard]] virtual MachineSemanticKind semantic_kind(ir::OpCode opcode) const noexcept = 0;
    [[nodiscard]] virtual bool is_runtime_backed(ir::OpCode opcode) const noexcept = 0;
    [[nodiscard]] virtual std::span<const std::string_view> rule_lines() const noexcept = 0;
};

class TargetISelRegistry {
public:
    using Provider = const TargetISel* (*)(core::ArchType) noexcept;

    static void register_provider(Provider provider) noexcept {
        provider_ = provider;
    }

    [[nodiscard]] static const TargetISel* get(const core::TargetMachine& target) noexcept {
        if (provider_ != nullptr) {
            return provider_(target.get_arch());
        }
        return nullptr;
    }

private:
    inline static Provider provider_ = nullptr;
};

[[nodiscard]] inline MachineForm classify_machine_form(
    const core::TargetMachine& target, ir::OpCode opcode) noexcept {
    const TargetISel* isel = TargetISelRegistry::get(target);
    if (isel != nullptr) {
        return isel->classify_machine_form(opcode);
    }
    return MachineForm::Unhandled;
}

[[nodiscard]] inline MachineForm classify_machine_node(
    const core::TargetMachine& target, const ir::IRNode& node) noexcept {
    const TargetISel* isel = TargetISelRegistry::get(target);
    if (isel != nullptr) {
        return isel->classify_machine_node(node);
    }
    return MachineForm::Unhandled;
}

[[nodiscard]] inline MachineSemanticKind semantic_kind(
    const core::TargetMachine& target, ir::OpCode opcode) noexcept {
    const TargetISel* isel = TargetISelRegistry::get(target);
    if (isel != nullptr) {
        return isel->semantic_kind(opcode);
    }
    return MachineSemanticKind::None;
}

[[nodiscard]] inline bool is_runtime_backed(
    const core::TargetMachine& target, ir::OpCode opcode) noexcept {
    const TargetISel* isel = TargetISelRegistry::get(target);
    if (isel != nullptr) {
        return isel->is_runtime_backed(opcode);
    }
    return false;
}

[[nodiscard]] inline std::span<const std::string_view> rule_lines(
    const core::TargetMachine& target) noexcept {
    const TargetISel* isel = TargetISelRegistry::get(target);
    if (isel != nullptr) {
        return isel->rule_lines();
    }
    return {};
}

} // namespace dgtl::mir
