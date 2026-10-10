/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Component:   RISC-V 64 Physical Register Mapping & Target Register Info
 * Standards:   C++26 / Clang 18+ / MSVC 19.40+ / GCC 14+
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */
module;

#include <cstdlib>
#include <array>
#include <cstdint>
#include <string_view>
export module dgtl.isa.riscv64.riscv64_reg_map;
import dgtl.mir.machine_ir;
export namespace dgtl::isa::riscv64 {

enum class RV64Reg : std::uint8_t {
    X0_ZERO = 0, X1_RA,   X2_SP,   X3_GP,   X4_TP,   X5_T0,   X6_T1,   X7_T2,
    X8_S0_FP,    X9_S1,   X10_A0,  X11_A1,  X12_A2,  X13_A3,  X14_A4,  X15_A5,
    X16_A6,      X17_A7,  X18_S2,  X19_S3,  X20_S4,  X21_S5,  X22_S6,  X23_S7,
    X24_S8,      X25_S9,  X26_S10, X27_S11, X28_T3,  X29_T4,  X30_T5,  X31_T6,
    INVALID = 0xFF
};

enum class RV64FpReg : std::uint8_t {
    F0_FT0 = 0,  F1_FT1,  F2_FT2,  F3_FT3,  F4_FT4,  F5_FT5,  F6_FT6,  F7_FT7,
    F8_FS0,      F9_FS1,  F10_FA0, F11_FA1, F12_FA2, F13_FA3, F14_FA4, F15_FA5,
    F16_FA6,     F17_FA7, F18_FS2, F19_FS3, F20_FS4, F21_FS5, F22_FS6, F23_FS7,
    F24_FS8,     F25_FS9, F26_FS10,F27_FS11,F28_FT8, F29_FT9, F30_FT10,F31_FT11,
    INVALID = 0xFF
};

class RiscV64TargetRegisterInfo final : public dgtl::machine::TargetRegisterInfo {
public:
    [[nodiscard]] std::size_t physical_register_count() const noexcept override {
        // 32 GPRs (0..31) + 32 FPRs (32..63) + 32 Vector registers (64..95)
        return 96u;
    }

    [[nodiscard]] bool is_allocatable(dgtl::machine::MachineRegister reg) const noexcept override {
        if (!reg.is_physical()) return false;

        // GPRs: x0 (zero), x2 (sp), x3 (gp), x4 (tp), x8 (s0/fp) are reserved
        if (reg.id < 32u) {
            if (reg.id == 0u || reg.id == 2u || reg.id == 3u || reg.id == 4u || reg.id == 8u) {
                return false;
            }
            return true;
        }

        // FPRs: f0..f31 are all allocatable
        if (reg.id >= 32u && reg.id < 64u) {
            return true;
        }

        // Vector registers: v0..v31 are all allocatable
        if (reg.id >= 64u && reg.id < 96u) {
            return true;
        }

        return false;
    }

    [[nodiscard]] dgtl::machine::RegisterClassKind
    register_class(dgtl::machine::MachineRegister reg) const noexcept override {
        if (!reg.is_physical()) return dgtl::machine::RegisterClassKind::Invalid;
        if (reg.id < 32u) {
            return dgtl::machine::RegisterClassKind::General;
        }
        if (reg.id >= 32u && reg.id < 64u) {
            return dgtl::machine::RegisterClassKind::FloatingPoint;
        }
        if (reg.id >= 64u && reg.id < 96u) {
            return dgtl::machine::RegisterClassKind::Vector;
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
        // x0 (zero), x2 (sp), x3 (gp), x4 (tp), x8 (s0/fp)
        return reg.id == 0u || reg.id == 2u || reg.id == 3u || reg.id == 4u || reg.id == 8u;
    }

    [[nodiscard]] bool is_caller_saved(dgtl::machine::MachineRegister reg) const noexcept override {
        if (!reg.is_physical()) return false;
        // GPR caller-saved: ra(1), t0-t2(5-7), a0-a7(10-17), t3-t6(28-31)
        if (reg.id == 1u || (reg.id >= 5u && reg.id <= 7u) ||
            (reg.id >= 10u && reg.id <= 17u) || (reg.id >= 28u && reg.id <= 31u)) {
            return true;
        }
        // FPR caller-saved: ft0-ft7(32-39), fa0-fa7(42-49), ft8-ft11(60-63)
        if ((reg.id >= 32u && reg.id <= 39u) || (reg.id >= 42u && reg.id <= 49u) ||
            (reg.id >= 60u && reg.id <= 63u)) {
            return true;
        }
        // Vector registers are caller-saved
        if (reg.id >= 64u && reg.id < 96u) {
            return true;
        }
        return false;
    }

    [[nodiscard]] bool is_callee_saved(dgtl::machine::MachineRegister reg) const noexcept override {
        if (!reg.is_physical()) return false;
        // GPR callee-saved: sp(2), s0/fp(8), s1(9), s2-s11(18-27)
        if (reg.id == 2u || reg.id == 8u || reg.id == 9u || (reg.id >= 18u && reg.id <= 27u)) {
            return true;
        }
        // FPR callee-saved: fs0-fs1(40-41), fs2-fs11(50-59)
        if ((reg.id >= 40u && reg.id <= 41u) || (reg.id >= 50u && reg.id <= 59u)) {
            return true;
        }
        return false;
    }
};

} // namespace dgtl::isa::riscv64
