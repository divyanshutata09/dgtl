/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Component:   CUDA PTX Virtual Register Model & Target Register Info
 * Standards:   C++26 / Clang 18+ / MSVC 19.40+ / GCC 14+
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */
module;

#include <cstdlib>
#include <cstdint>
#include <string_view>
export module dgtl.isa.ptx.cuda_reg_map;
import dgtl.mir.machine_ir;
export namespace dgtl::isa::ptx {

class CUDATargetRegisterInfo final : public dgtl::machine::TargetRegisterInfo {
public:
    [[nodiscard]] std::size_t physical_register_count() const noexcept override {
        // PTX models 255 allocatable hardware virtual registers per warp
        return 255u;
    }

    [[nodiscard]] bool is_allocatable(dgtl::machine::MachineRegister reg) const noexcept override {
        if (!reg.is_physical()) return false;
        return reg.id < 255u;
    }

    [[nodiscard]] dgtl::machine::RegisterClassKind
    register_class(dgtl::machine::MachineRegister reg) const noexcept override {
        if (!reg.is_physical()) return dgtl::machine::RegisterClassKind::Invalid;
        if (reg.reg_class != dgtl::machine::RegisterClassKind::Invalid) {
            return reg.reg_class;
        }
        return dgtl::machine::RegisterClassKind::General;
    }

    [[nodiscard]] bool aliases(dgtl::machine::MachineRegister lhs,
                               dgtl::machine::MachineRegister rhs) const noexcept override {
        if (!lhs.is_physical() || !rhs.is_physical()) return false;
        return lhs.id == rhs.id;
    }

    [[nodiscard]] bool is_reserved(dgtl::machine::MachineRegister) const noexcept override {
        return false;
    }

    [[nodiscard]] bool is_caller_saved(dgtl::machine::MachineRegister) const noexcept override {
        return true;
    }

    [[nodiscard]] bool is_callee_saved(dgtl::machine::MachineRegister) const noexcept override {
        return false;
    }
};

} // namespace dgtl::isa::ptx::cuda
