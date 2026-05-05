/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:    DGTL (Deep General-purpose Transformation Layer)
 * Owner:      Divyanshu Tata
 * Repository: github.com/TataG-Systems/DGTL
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

/* * DGTL (Deep General-purpose Transformation Layer) version 1.0.0 : THE COMPILER SPEC
 * STEP 42: X64 REGISTER MAPPING & MODR/M ENCODER
 * PILLAR 4: HARDWARE-NATIVE ENCODING
 * DESIGN: Maps DGTL Virtual Registers to Physical x64 GPRs.
 */

#pragma once
#include <array>
#include <cstdint>
#include <unordered_map>

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

enum class X64ZmmReg : uint8_t {
    ZMM0 = 0, ZMM1 = 1, ZMM2 = 2, ZMM3 = 3,
    ZMM4 = 4, ZMM5 = 5, ZMM6 = 6, ZMM7 = 7,
    ZMM8 = 8, ZMM9 = 9, ZMM10 = 10, ZMM11 = 11,
    ZMM12 = 12, ZMM13 = 13, ZMM14 = 14, ZMM15 = 15,
    ZMM16 = 16, ZMM17 = 17, ZMM18 = 18, ZMM19 = 19,
    ZMM20 = 20, ZMM21 = 21, ZMM22 = 22, ZMM23 = 23,
    ZMM24 = 24, ZMM25 = 25, ZMM26 = 26, ZMM27 = 27,
    ZMM28 = 28, ZMM29 = 29, ZMM30 = 30, ZMM31 = 31,
    INVALID = 0xFF
};

class X64SinglePassRegisterMapper {
private:
    static constexpr std::array<X64Reg, 5> kNonVolatileGprs = {
        X64Reg::RBX, X64Reg::R12, X64Reg::R13, X64Reg::R14, X64Reg::R15,
    };

    static constexpr std::array<X64Reg, 4> kVolatileGprs = {
        X64Reg::R8, X64Reg::R9, X64Reg::R10, X64Reg::R11,
    };

    static constexpr std::array<X64Reg, 9> kAllocatableGprs = {
        X64Reg::RBX, X64Reg::R12, X64Reg::R13, X64Reg::R14, X64Reg::R15,
        X64Reg::R8,  X64Reg::R9,  X64Reg::R10, X64Reg::R11,
    };

public:
    static constexpr uint32_t GprWindow = static_cast<uint32_t>(kAllocatableGprs.size());
    static constexpr uint32_t ZmmWindow = 32;
    static constexpr uint32_t NonVolatileWindow = static_cast<uint32_t>(kNonVolatileGprs.size());
    static constexpr uint32_t VolatileWindow = static_cast<uint32_t>(kVolatileGprs.size());

    static_assert(GprWindow > 0, "x64 register mapper must expose at least one allocatable GPR");
    static_assert((ZmmWindow & (ZmmWindow - 1)) == 0, "ZmmWindow must remain power-of-two for bit-mask mapping");

    [[nodiscard]] static constexpr X64Reg map_gpr_branchless(uint32_t virtual_id) noexcept {
        return kAllocatableGprs[virtual_id % GprWindow];
    }

    [[nodiscard]] static constexpr X64Reg map_gpr_prefer_nonvolatile(uint32_t virtual_id) noexcept {
        return kNonVolatileGprs[virtual_id % NonVolatileWindow];
    }

    [[nodiscard]] static constexpr X64Reg map_volatile_only(uint32_t virtual_id) noexcept {
        return kVolatileGprs[virtual_id % VolatileWindow];
    }

    [[nodiscard]] static constexpr X64ZmmReg map_zmm_circular(uint32_t virtual_id) noexcept {
        return static_cast<X64ZmmReg>(virtual_id & (ZmmWindow - 1));
    }

    [[nodiscard]] static constexpr uint8_t zmm_index(uint32_t virtual_id) noexcept {
        return static_cast<uint8_t>(map_zmm_circular(virtual_id));
    }
};

enum class X64StorageKind : uint8_t {
    register_lock,
    stack_slot
};

struct X64StorageBinding {
    X64StorageKind kind = X64StorageKind::register_lock;
    X64Reg reg = X64Reg::INVALID;
    int32_t stack_offset = 0;
    bool pinned = true;
};

class X64LocalStateLockTable {
private:
    std::unordered_map<uint32_t, X64StorageBinding> locks_;

public:
    void clear() {
        locks_.clear();
    }

    void lock_to_register(uint32_t virtual_id, X64Reg reg) {
        if (reg == X64Reg::INVALID) {
            return;
        }
        locks_[virtual_id] = X64StorageBinding{X64StorageKind::register_lock, reg, 0, true};
    }

    void lock_to_stack(uint32_t virtual_id, int32_t stack_offset) {
        locks_[virtual_id] = X64StorageBinding{X64StorageKind::stack_slot, X64Reg::INVALID, stack_offset, true};
    }

    [[nodiscard]] bool resolve(uint32_t virtual_id, X64StorageBinding& out) const {
        const auto it = locks_.find(virtual_id);
        if (it == locks_.end()) {
            return false;
        }
        out = it->second;
        return true;
    }

    [[nodiscard]] bool is_locked(uint32_t virtual_id) const {
        return locks_.find(virtual_id) != locks_.end();
    }
};

class X64Encoding {
public:
    [[nodiscard]] static constexpr uint8_t reg_index(X64Reg reg) noexcept {
        return static_cast<uint8_t>(reg);
    }

    [[nodiscard]] static constexpr bool is_valid(X64Reg reg) noexcept {
        return reg != X64Reg::INVALID;
    }

    [[nodiscard]] static constexpr bool is_valid(X64ZmmReg reg) noexcept {
        return reg != X64ZmmReg::INVALID;
    }

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
    [[nodiscard]] static constexpr X64Reg map_DGTL_to_phys(uint32_t DGTL_id) noexcept {
        return X64SinglePassRegisterMapper::map_gpr_branchless(DGTL_id);
    }

    [[nodiscard]] static constexpr uint8_t map_DGTL_vec_to_phys(uint32_t DGTL_id) noexcept {
        return X64SinglePassRegisterMapper::zmm_index(DGTL_id);
    }

    /**
     * @brief Check if a register requires a REX prefix extension bit (> 7).
     */
    static constexpr inline bool requires_extension(X64Reg reg) noexcept {
        const uint8_t idx = reg_index(reg);
        return idx >= 8 && idx != 0xFF;
    }
};

} // namespace dgtl::exec

