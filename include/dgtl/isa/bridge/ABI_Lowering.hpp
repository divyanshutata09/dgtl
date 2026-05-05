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

#pragma once

#include <cstdint>
#include <vector>
#include <array>

namespace dgtl {
namespace isa {

/**
 * @enum ABIConvention
 * @brief Target calling convention/ABI specification.
 */
enum class ABIConvention : uint8_t {
    SystemV_x64 = 0,    ///< System V AMD64 ABI (Linux/BSD standard)
    Microsoft_x64 = 1,  ///< Microsoft x64 calling convention (Windows)
    ARM64_EABI = 2,     ///< ARM64 EABI (AArch64)
    WebAssembly = 3     ///< WebAssembly (wasm32/wasm64)
};

/**
 * @struct ABIParameter
 * @brief Describes parameter location and properties after lowering.
 */
struct ABIParameter {
    uint32_t register_id;    ///< Register number if in_register=true, else undefined
    uint32_t stack_offset;   ///< Byte offset from frame pointer if !in_register
    uint32_t size;           ///< Parameter size in bytes
    bool in_register;        ///< true = use register, false = use stack
};

/**
 * @class ABI_Lowering
 * @brief Converts high-level parameters to ABI-conformant register/stack layout.
 * 
 * Implements the calling convention for various ISA targets.
 * Used during codegen to map virtual registers to physical locations.
 */
class ABI_Lowering {
public:
    /**
     * @brief Create lowering engine for target convention.
     * @param convention Target ABI (defaults to System V x64)
     */
    explicit ABI_Lowering(ABIConvention convention = ABIConvention::SystemV_x64) noexcept
        : convention_(convention), stack_offset_(0) {}

    ~ABI_Lowering() = default;

    ABI_Lowering(const ABI_Lowering&) = delete;
    ABI_Lowering& operator=(const ABI_Lowering&) = delete;

    /**
     * @brief Lower a parameter to its ABI location.
     * @param index Argument index (0 = first arg, etc.)
     * @param size Parameter size in bytes
     * @return ABIParameter describing register/stack layout
     */
    [[nodiscard]] ABIParameter lower_parameter(uint32_t index, uint32_t size) noexcept;

    /**
     * @brief Get the register ID for return values.
     * @return Register ID (typically RAX/RDX pair for System V)
     */
    [[nodiscard]] uint32_t get_return_register() const noexcept;

    /**
     * @brief Get the register ID for the stack pointer.
     * @return Register ID (typically RSP for x64, SP for ARM64)
     */
    [[nodiscard]] uint32_t get_stack_pointer_register() const noexcept;

    /**
     * @brief Get the register ID for the frame pointer.
     * @return Register ID (typically RBP for x64, FP for ARM64)
     */
    [[nodiscard]] uint32_t get_frame_pointer_register() const noexcept;

    /**
     * @brief Check if parameter fits in registers for this convention.
     * @return true if at least one more register parameter is available
     */
    [[nodiscard]] bool has_register_slots() const noexcept;

    /**
     * @brief Reset state for a new function (clear stack offset, etc.).
     */
    void reset() noexcept {
        stack_offset_ = 0;
    }

    /**
     * @brief Get current target convention.
     */
    [[nodiscard]] ABIConvention get_convention() const noexcept { return convention_; }

private:
    // System V x64: first 6 integer args in rdi, rsi, rdx, rcx, r8, r9
    static constexpr std::array<uint32_t, 6> SYSTEMV_INT_REGS = {
        0,   // RDI (arg 0)
        1,   // RSI (arg 1)
        2,   // RDX (arg 2)
        3,   // RCX (arg 3)
        4,   // R8  (arg 4)
        5    // R9  (arg 5)
    };

    // Microsoft x64: first 4 integer args in rcx, rdx, r8, r9
    static constexpr std::array<uint32_t, 4> MICROSOFT_INT_REGS = {
        0,   // RCX (arg 0)
        1,   // RDX (arg 1)
        4,   // R8  (arg 2)
        5    // R9  (arg 3)
    };

    // ARM64: first 8 integer args in x0-x7
    static constexpr std::array<uint32_t, 8> ARM64_INT_REGS = {
        0, 1, 2, 3, 4, 5, 6, 7  // x0-x7
    };

    ABIConvention convention_;
    uint32_t stack_offset_;
};

} // namespace isa
} // namespace dgtl
