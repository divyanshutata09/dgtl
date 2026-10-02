/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Component:   SPIR-V Target Register & SSA ID Mapping
 * Standards:   C++26 / Clang 18+ / MSVC 19.40+ / GCC 14+
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */
module;

#include <cstdlib>
#include <cstdint>
#include <string_view>

export module dgtl.isa.spirv.reg_map;

import dgtl.mir.machine_ir;

export namespace dgtl::isa::spirv {

/**
 * @brief SPIR-V Register & SSA ID Mapping.
 * SPIR-V instructions produce and consume SSA IDs (uint32_t IDs).
 * In DGTL's MIR, these are modeled as machine registers with arbitrary IDs.
 */
class SPIRVTargetRegisterInfo final : public dgtl::machine::TargetRegisterInfo {
public:
    [[nodiscard]] std::size_t physical_register_count() const noexcept override {
        // SPIR-V uses an SSA virtual register ID space (up to bound ID)
        return 65536u;
    }

    [[nodiscard]] bool is_allocatable(dgtl::machine::MachineRegister reg) const noexcept override {
        // In SPIR-V SSA, ID 0 is invalid/unassigned; positive IDs are valid
        return reg.id != 0u;
    }

    [[nodiscard]] dgtl::machine::RegisterClassKind
    register_class(dgtl::machine::MachineRegister reg) const noexcept override {
        return reg.reg_class;
    }

    [[nodiscard]] bool aliases(dgtl::machine::MachineRegister lhs,
                               dgtl::machine::MachineRegister rhs) const noexcept override {
        return lhs.id == rhs.id && lhs.kind == rhs.kind;
    }

    [[nodiscard]] bool is_reserved(dgtl::machine::MachineRegister reg) const noexcept override {
        return reg.id == 0u;
    }

    [[nodiscard]] bool is_caller_saved(dgtl::machine::MachineRegister reg) const noexcept override {
        (void)reg;
        return true;
    }

    [[nodiscard]] bool is_callee_saved(dgtl::machine::MachineRegister reg) const noexcept override {
        (void)reg;
        return false;
    }
};

/**
 * @brief Maps a DGTL MachineRegister to a valid SPIR-V ID (offsetting by base ID if needed).
 */
[[nodiscard]] constexpr std::uint32_t to_spirv_id(dgtl::machine::MachineRegister reg, std::uint32_t base_id = 100u) noexcept {
    return reg.id + base_id;
}

} // namespace dgtl::isa::spirv
