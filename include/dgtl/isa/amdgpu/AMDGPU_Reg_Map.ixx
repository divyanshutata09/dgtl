/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Component:   ROCm / AMDGPU GCN VGPR/SGPR Register Model & Target Register Info
 * Standards:   C++26 / Clang 18+ / MSVC 19.40+ / GCC 14+
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */
module;

#include <cstdlib>
#include <cstdint>
#include <string_view>
export module dgtl.isa.amdgpu.rocm_reg_map;
import dgtl.mir.machine_ir;
export namespace dgtl::isa::amdgpu {

class ROCmTargetRegisterInfo final : public dgtl::machine::TargetRegisterInfo {
public:
    [[nodiscard]] std::size_t physical_register_count() const noexcept override {
        // AMDGPU: 256 VGPRs (0..255) + 106 SGPRs (256..361)
        return 362u;
    }

    [[nodiscard]] bool is_allocatable(dgtl::machine::MachineRegister reg) const noexcept override {
        if (!reg.is_physical()) return false;
        // SGPRs s104-s105 are reserved for flat scratch/VCC/EXEC
        if (reg.id >= 256u && reg.id >= 360u) {
            return false;
        }
        return reg.id < 362u;
    }

    [[nodiscard]] dgtl::machine::RegisterClassKind
    register_class(dgtl::machine::MachineRegister reg) const noexcept override {
        if (!reg.is_physical()) return dgtl::machine::RegisterClassKind::Invalid;
        if (reg.id < 256u) {
            return dgtl::machine::RegisterClassKind::Vector; // VGPR
        }
        if (reg.id >= 256u && reg.id < 362u) {
            return dgtl::machine::RegisterClassKind::General; // SGPR
        }
        return dgtl::machine::RegisterClassKind::Invalid;
    }

    [[nodiscard]] bool aliases(dgtl::machine::MachineRegister lhs,
                               dgtl::machine::MachineRegister rhs) const noexcept override {
        if (!lhs.is_physical() || !rhs.is_physical()) return false;
        return lhs.id == rhs.id;
    }

    [[nodiscard]] bool is_reserved(dgtl::machine::MachineRegister reg) const noexcept override {
        if (!reg.is_physical()) return false;
        // SGPRs s104-s105 reserved
        return reg.id >= 360u;
    }

    [[nodiscard]] bool is_caller_saved(dgtl::machine::MachineRegister) const noexcept override {
        return true;
    }

    [[nodiscard]] bool is_callee_saved(dgtl::machine::MachineRegister) const noexcept override {
        return false;
    }
};

} // namespace dgtl::isa::amdgpu::rocm
