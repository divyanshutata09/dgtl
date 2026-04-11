/* * DGTL-MLIR-X v1.0 : THE COMPILER SPEC
 * STEP 42: X64 REGISTER MAPPING & MODR/M ENCODER
 * PILLAR 4: HARDWARE-NATIVE ENCODING
 * DESIGN: Maps DGTL Virtual Registers to Physical x64 GPRs.
 */

#pragma once
#include <cstdint>

namespace dgtl::exec {

/**
 * @brief Physical x86_64 Register Indices
 * Standardized for DGTL's High-Performance calling convention.
 * Note: R12-R15 are often callee-saved; DGTL handles this at the Strike prologue.
 */
enum class X64Reg : uint8_t {
    RAX = 0, RCX = 1, RDX = 2, RBX = 3,
    RSP = 4, RBP = 5, RSI = 6, RDI = 7,
    R8  = 8, R9  = 9, R10 = 10, R11 = 11,
    R12 = 12, R13 = 13, R14 = 14, R15 = 15,
    INVALID = 0xFF
};

class X64Encoding {
public:
    /**
     * @brief Encodes the REX prefix for 64-bit operands.
     * @param w Set for 64-bit operand size (REX.W).
     * @param r Extension of the ModR/M 'reg' field (REX.R).
     * @param x Extension of the SIB 'index' field (REX.X).
     * @param b Extension of the ModR/M 'r/m' field or SIB 'base' field (REX.B).
     */
    static constexpr inline uint8_t rex(bool w, uint8_t r, uint8_t x, uint8_t b) noexcept {
        uint8_t res = 0x40;
        if (w) res |= 0x08;         // Bit 3: 64-bit operand size
        if (r & 0x08) res |= 0x04;  // Bit 2: Extension of ModR/M reg
        if (x & 0x08) res |= 0x02;  // Bit 1: Extension of SIB index
        if (b & 0x08) res |= 0x01;  // Bit 0: Extension of ModR/M r/m or SIB base
        return res;
    }

    /**
     * @brief Encodes the ModR/M byte.
     * @param mod 00=Reg Indirect, 01=8-bit disp, 10=32-bit disp, 11=Reg-to-Reg.
     * @param reg The source/destination register (3 bits).
     * @param rm  The second register or memory base (3 bits).
     */
    static constexpr inline uint8_t modrm(uint8_t mod, uint8_t reg, uint8_t rm) noexcept {
        return ((mod & 0x03) << 6) | ((reg & 0x07) << 3) | (rm & 0x07);
    }

    /**
     * @brief Encodes the SIB (Scale-Index-Base) byte.
     * @param ss Scale (0=1, 1=2, 2=4, 3=8).
     * @param index Index register index.
     * @param base Base register index.
     */
    static constexpr inline uint8_t sib(uint8_t ss, uint8_t index, uint8_t base) noexcept {
        return ((ss & 0x03) << 6) | ((index & 0x07) << 3) | (base & 0x07);
    }

    /**
     * @brief Helper to map DGTL's internal register ID to physical X64Reg.
     * PILLAR 4: Strictly isolates RSP and RBP to prevent stack corruption.
     * Mapping Table:
     * DGTL ID [0..5] -> RAX, RBX, RCX, RDX, RSI, RDI
     * DGTL ID [6..15] -> R8..R15
     */
    static constexpr X64Reg map_DGTL_to_phys(uint32_t DGTL_id) noexcept {
        switch (DGTL_id) {
            case 0: return X64Reg::RAX;
            case 1: return X64Reg::RBX;
            case 2: return X64Reg::RCX;
            case 3: return X64Reg::RDX;
            case 4: return X64Reg::RSI;
            case 5: return X64Reg::RDI;
            // Gap 4 (RSP) and 5 (RBP) are skipped for stability
            default: 
                // [DGTL UPDATE]: Expanded mapping to include R14 and R15
                // Map DGTL IDs 6 through 15 to R8 through R15
                if (DGTL_id >= 6 && DGTL_id <= 15) {
                    return static_cast<X64Reg>(8 + (DGTL_id - 6));
                }
                return X64Reg::INVALID;
        }
    }

    /**
     * @brief Check if a register requires a REX prefix extension bit (> 7).
     */
    static constexpr inline bool requires_extension(X64Reg reg) noexcept {
        return static_cast<uint8_t>(reg) >= 8 && static_cast<uint8_t>(reg) != 0xFF;
    }
};

} // namespace dgtl::exec
