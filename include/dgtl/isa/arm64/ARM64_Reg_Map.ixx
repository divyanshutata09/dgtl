/*
 * DGTL ARM64/AArch64 target register model.
 *
 * Physical IDs:
 *   X0-X30  -> 0-30
 *   SP      -> 31
 *   V0-V31  -> 32-63
 *
 * Scalar FP and vector values intentionally share the V-register bank.
 * The RegisterClassKind distinguishes scalar/vector use while the physical
 * ID preserves the architectural aliasing relationship.
 */
module;

#include <cstddef>
#include <cstdint>
export module dgtl.isa.arm64.arm64_reg_map;
import dgtl.mir.machine_ir;
export namespace dgtl::isa::arm64 {

enum class ARM64GPR : std::uint8_t {
    X0=0, X1, X2, X3, X4, X5, X6, X7,
    X8, X9, X10, X11, X12, X13, X14, X15,
    X16, X17, X18, X19, X20, X21, X22, X23,
    X24, X25, X26, X27, X28, X29, X30, SP
};

inline constexpr std::uint32_t ARM64_VREG_BASE = 32u;

[[nodiscard]] constexpr dgtl::machine::MachineRegister arm64_x(
    std::uint32_t id,
    dgtl::machine::RegisterClassKind cls = dgtl::machine::RegisterClassKind::General) noexcept {
    return {id, dgtl::machine::RegisterKind::Physical, cls};
}

[[nodiscard]] constexpr dgtl::machine::MachineRegister arm64_v(
    std::uint32_t id,
    dgtl::machine::RegisterClassKind cls = dgtl::machine::RegisterClassKind::Vector) noexcept {
    return {ARM64_VREG_BASE + id, dgtl::machine::RegisterKind::Physical, cls};
}

class ARM64TargetRegisterInfo final : public dgtl::machine::TargetRegisterInfo {
public:
    [[nodiscard]] std::size_t physical_register_count() const noexcept override {
        return 64u;
    }

    [[nodiscard]] bool is_allocatable(dgtl::machine::MachineRegister reg) const noexcept override {
        if (!reg.is_physical()) return false;

        if (reg.reg_class == dgtl::machine::RegisterClassKind::General ||
            reg.reg_class == dgtl::machine::RegisterClassKind::Integer ||
            reg.reg_class == dgtl::machine::RegisterClassKind::Address) {
            // AAPCS64 callee-saved GPRs.  Using the non-volatile bank keeps
            // ordinary MIR values live across calls without hidden call-site
            // save/restore logic.
            return reg.id >= 19u && reg.id <= 28u;
        }

        if (reg.reg_class == dgtl::machine::RegisterClassKind::FloatingPoint) {
            // Scalar FP values may use V8-V15 because AAPCS64 preserves their
            // low 64 bits; the frame pass saves the exact D-registers used.
            return reg.id >= 40u && reg.id <= 47u;
        }

        if (reg.reg_class == dgtl::machine::RegisterClassKind::Vector) {
            // Full-width vectors use caller-saved V16-V31.
            return reg.id >= 48u && reg.id <= 63u;
        }

        return false;
    }

    [[nodiscard]] dgtl::machine::RegisterClassKind
    register_class(dgtl::machine::MachineRegister reg) const noexcept override {
        if (!reg.is_physical()) return dgtl::machine::RegisterClassKind::Invalid;
        if (reg.id < 31u) return dgtl::machine::RegisterClassKind::General;
        if (reg.id == 31u) return dgtl::machine::RegisterClassKind::Special;
        if (reg.id >= 32u && reg.id < 64u) {
            return reg.reg_class == dgtl::machine::RegisterClassKind::FloatingPoint
                ? dgtl::machine::RegisterClassKind::FloatingPoint
                : dgtl::machine::RegisterClassKind::Vector;
        }
        return dgtl::machine::RegisterClassKind::Invalid;
    }

    [[nodiscard]] bool aliases(dgtl::machine::MachineRegister lhs,
                                dgtl::machine::MachineRegister rhs) const noexcept override {
        if (!lhs.is_physical() || !rhs.is_physical()) return false;
        if (lhs.id == rhs.id) return true;

        // Vn/Sn/Dn are views of the same 128-bit architectural SIMD register.
        const bool lhs_simd =
            lhs.id >= ARM64_VREG_BASE && lhs.id < ARM64_VREG_BASE + 32u;
        const bool rhs_simd =
            rhs.id >= ARM64_VREG_BASE && rhs.id < ARM64_VREG_BASE + 32u;
        return lhs_simd && rhs_simd &&
               (lhs.reg_class == dgtl::machine::RegisterClassKind::FloatingPoint ||
                lhs.reg_class == dgtl::machine::RegisterClassKind::Vector) &&
               (rhs.reg_class == dgtl::machine::RegisterClassKind::FloatingPoint ||
                rhs.reg_class == dgtl::machine::RegisterClassKind::Vector);
    }

    [[nodiscard]] bool is_reserved(dgtl::machine::MachineRegister reg) const noexcept override {
        if (!reg.is_physical()) return false;
        if (reg.id <= 18u) return true; // argument/return, IP0/IP1, platform/scratch bank
        if (reg.id == 29u || reg.id == 30u || reg.id == 31u) return true; // FP/LR/SP
        return false;
    }

    [[nodiscard]] bool is_caller_saved(dgtl::machine::MachineRegister reg) const noexcept override {
        if (!reg.is_physical()) return false;
        if (reg.reg_class == dgtl::machine::RegisterClassKind::General ||
            reg.reg_class == dgtl::machine::RegisterClassKind::Integer ||
            reg.reg_class == dgtl::machine::RegisterClassKind::Address) {
            return reg.id <= 18u || reg.id == 30u;
        }
        if (reg.reg_class == dgtl::machine::RegisterClassKind::FloatingPoint ||
            reg.reg_class == dgtl::machine::RegisterClassKind::Vector) {
            return (reg.id >= 32u && reg.id <= 39u) ||
                   (reg.id >= 48u && reg.id <= 63u);
        }
        return false;
    }

    [[nodiscard]] bool is_callee_saved(dgtl::machine::MachineRegister reg) const noexcept override {
        if (!reg.is_physical()) return false;
        if (reg.reg_class == dgtl::machine::RegisterClassKind::General ||
            reg.reg_class == dgtl::machine::RegisterClassKind::Integer ||
            reg.reg_class == dgtl::machine::RegisterClassKind::Address) {
            return reg.id >= 19u && reg.id <= 28u;
        }
        // V8-V15 are AAPCS64 non-volatile, but are intentionally excluded from
        // the current allocator until the common frame contract can represent
        // 16-byte SIMD saves. This method remains ABI-accurate.
        if (reg.reg_class == dgtl::machine::RegisterClassKind::FloatingPoint ||
            reg.reg_class == dgtl::machine::RegisterClassKind::Vector) {
            return reg.id >= 40u && reg.id <= 47u;
        }
        return false;
    }
};

} // namespace dgtl::isa::arm64
