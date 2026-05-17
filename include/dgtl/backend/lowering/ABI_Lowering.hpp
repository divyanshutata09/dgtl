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
#include <utility>

namespace dgtl::backend::lowering {

/**
 * @enum ABIConvention
 * @brief Target calling convention/ABI specification.
 */
enum class ABIConvention : uint8_t {
    SystemV_x64 = 0,    ///< System V AMD64 ABI (Linux/BSD/macOS standard)
    Microsoft_x64 = 1,  ///< Microsoft x64 calling convention (Windows)
    ARM64_EABI = 2,     ///< ARM64 EABI (AArch64 / Apple Silicon)
    WebAssembly = 3     ///< WebAssembly (wasm32/wasm64)
};

/**
 * @enum ABIValueClass
 * @brief Categorizes the data type to route to the correct hardware register bank.
 */
enum class ABIValueClass : uint8_t {
    IntegerOrPointer = 0, ///< Uses GPRs (e.g., RAX, X0)
    FloatingPoint = 1,    ///< Uses FPRs (e.g., XMM0, V0)
    Vector = 2,           ///< Uses SIMD registers (e.g., YMM, ZMM)
    Memory = 3            ///< Forced to stack (large structs)
};

/**
 * @struct ABIParameter
 * @brief Describes parameter location and properties after lowering.
 */
struct ABIParameter {
    uint32_t register_id;    ///< Hardware register ID (if in_register=true)
    uint32_t stack_offset;   ///< Byte offset from frame pointer (if in_register=false)
    uint32_t size;           ///< Parameter size in bytes
    bool in_register;        ///< true = bound to register, false = spilled to stack
};

struct ABICallLayout {
    std::vector<ABIParameter> parameters;
    uint32_t return_register = 0;
    uint32_t argument_stack_size = 0;
    bool uses_variadic_policy = false;
};

/**
 * @class ABI_Lowering
 * @brief Converts high-level parameters to ABI-conformant register/stack layout.
 * 
 * Implements the rigorous calling conventions for HFT/System-level targets,
 * handling shadow spaces, split register banks, and struct alignment.
 */
class ABI_Lowering {
public:
    /**
     * @brief Create lowering engine for target convention.
     * @param convention Target ABI (defaults to System V x64)
     */
    explicit ABI_Lowering(ABIConvention convention = ABIConvention::SystemV_x64) noexcept
        : convention_(convention) {
        reset();
    }

    ~ABI_Lowering() = default;

    ABI_Lowering(const ABI_Lowering&) = delete;
    ABI_Lowering& operator=(const ABI_Lowering&) = delete;

    /**
     * @brief Lower a parameter to its ABI location.
     * @param val_class Type class (Int/Ptr vs Float vs Vector)
     * @param size Parameter size in bytes
     * @param alignment Memory alignment requirement (e.g., 8 for double, 16 for vectors)
     * @return ABIParameter describing exact register or stack layout
     */
    [[nodiscard]] ABIParameter lower_parameter(ABIValueClass val_class, 
                                               uint32_t size, 
                                               uint32_t alignment = 8) noexcept;

    /**
     * @brief Lower a variadic argument with target-specific safety rules.
     * Variadic values are conservatively placed to preserve ABI correctness.
     */
    [[nodiscard]] ABIParameter lower_variadic_parameter(ABIValueClass val_class,
                                                        uint32_t size,
                                                        uint32_t alignment = 8) noexcept;

    /**
     * @brief Plan a full call ABI layout for deterministic argument assignment.
     */
    [[nodiscard]] ABICallLayout plan_call_layout(const std::vector<ABIValueClass>& value_classes,
                                                 const std::vector<uint32_t>& sizes,
                                                 ABIValueClass return_class,
                                                 bool is_variadic = false,
                                                 size_t fixed_parameter_count = 0) noexcept;

    /**
     * @brief Get the primary register ID for scalar return values.
     * @param val_class The return type class
     */
    [[nodiscard]] uint32_t get_return_register(ABIValueClass val_class) const noexcept;

    /**
     * @brief Get the hardware register ID for the stack pointer.
     */
    [[nodiscard]] constexpr uint32_t get_stack_pointer_register() const noexcept {
        return (convention_ == ABIConvention::ARM64_EABI) ? 31 : 4; /* SP vs RSP */
    }

    /**
     * @brief Get the hardware register ID for the frame pointer.
     */
    [[nodiscard]] constexpr uint32_t get_frame_pointer_register() const noexcept {
        return (convention_ == ABIConvention::ARM64_EABI) ? 29 : 5; /* X29 vs RBP */
    }

    /**
     * @brief Reset state for a new function compilation pass.
     * Re-initializes register counters and allocates MS x64 shadow space if needed.
     */
    void reset() noexcept {
        int_regs_used_ = 0;
        float_regs_used_ = 0;
        unified_slots_used_ = 0;
        
        // Windows x64 requires a mandatory 32-byte shadow space (home space) 
        // for the first 4 arguments, even if they are passed in registers.
        stack_offset_ = (convention_ == ABIConvention::Microsoft_x64) ? 32 : 0;
    }

    /**
     * @brief Get current target convention.
     */
    [[nodiscard]] constexpr ABIConvention get_convention() const noexcept { 
        return convention_; 
    }

    /**
     * @brief Get total stack size required for arguments.
     */
    [[nodiscard]] constexpr uint32_t get_argument_stack_size() const noexcept {
        return stack_offset_;
    }

    [[nodiscard]] uint32_t finalize_argument_stack_size(uint32_t call_alignment = 16) const noexcept {
        if (call_alignment == 0 || (call_alignment & (call_alignment - 1u)) != 0u) {
            call_alignment = 16;
        }
        return (stack_offset_ + call_alignment - 1u) & ~(call_alignment - 1u);
    }

private:
    ABIConvention convention_;
    
    // --- State Trackers for the current function being lowered ---
    uint8_t int_regs_used_ = 0;
    uint8_t float_regs_used_ = 0;
    uint8_t unified_slots_used_ = 0; ///< Used by Windows ABI (shares Int/Float slots)
    uint32_t stack_offset_ = 0;

    /**
     * @brief Aligns the current stack offset to the required boundary.
     */
    void align_stack(uint32_t alignment) noexcept {
        if (alignment > 1) {
            stack_offset_ = (stack_offset_ + alignment - 1) & ~(alignment - 1);
        }
    }

    // =========================================================================
    // HARDWARE REGISTER MAPPINGS
    // =========================================================================

    // System V x64: 6 Integer (RDI, RSI, RDX, RCX, R8, R9)
    static constexpr std::array<uint32_t, 6> SYSTEMV_INT_REGS = {
        0, 1, 2, 3, 4, 5 // SysV abstract IDs
    };

    // System V x64: 8 Float (XMM0-XMM7)
    static constexpr std::array<uint32_t, 8> SYSTEMV_FLOAT_REGS = {
        16, 17, 18, 19, 20, 21, 22, 23 // XMM mapped to IDs 16-23
    };

    // Microsoft x64: 4 Integer (RCX, RDX, R8, R9)
    static constexpr std::array<uint32_t, 4> MICROSOFT_INT_REGS = {
        3, 2, 4, 5 // Windows abstract IDs
    };

    // Microsoft x64: 4 Float (XMM0-XMM3) mapped to unified slots
    static constexpr std::array<uint32_t, 4> MICROSOFT_FLOAT_REGS = {
        16, 17, 18, 19
    };

    // ARM64: 8 Integer (X0-X7)
    static constexpr std::array<uint32_t, 8> ARM64_INT_REGS = {
        0, 1, 2, 3, 4, 5, 6, 7
    };

    // ARM64: 8 Float (V0-V7)
    static constexpr std::array<uint32_t, 8> ARM64_FLOAT_REGS = {
        32, 33, 34, 35, 36, 37, 38, 39 // Vector register abstract IDs
    };
};

inline ABIParameter ABI_Lowering::lower_parameter(ABIValueClass val_class,
                                                  uint32_t size,
                                                  uint32_t alignment) noexcept {
    if (size == 0) {
        size = 8;
    }

    if (alignment == 0 || (alignment & (alignment - 1u)) != 0u) {
        alignment = 8;
    }

    const auto spill_to_stack = [&]() noexcept {
        align_stack(alignment);
        const uint32_t offset = stack_offset_;
        const uint32_t rounded = (size + 7u) & ~7u;
        stack_offset_ += rounded;
        return ABIParameter{0xFFFFFFFFu, offset, size, false};
    };

    // Large aggregates and explicit memory-class values always spill.
    if (val_class == ABIValueClass::Memory || size > 16u) {
        return spill_to_stack();
    }

    const bool fp_bank = (val_class == ABIValueClass::FloatingPoint ||
                          val_class == ABIValueClass::Vector);

    switch (convention_) {
        case ABIConvention::SystemV_x64: {
            if (fp_bank) {
                if (float_regs_used_ < SYSTEMV_FLOAT_REGS.size()) {
                    const uint32_t reg = SYSTEMV_FLOAT_REGS[float_regs_used_++];
                    return ABIParameter{reg, 0u, size, true};
                }
            } else {
                if (int_regs_used_ < SYSTEMV_INT_REGS.size()) {
                    const uint32_t reg = SYSTEMV_INT_REGS[int_regs_used_++];
                    return ABIParameter{reg, 0u, size, true};
                }
            }
            return spill_to_stack();
        }

        case ABIConvention::Microsoft_x64: {
            // Microsoft x64 uses a shared 4-slot argument window for integer + FP.
            if (unified_slots_used_ < 4u) {
                const uint8_t slot = unified_slots_used_++;
                if (fp_bank) {
                    return ABIParameter{MICROSOFT_FLOAT_REGS[slot], 0u, size, true};
                }
                return ABIParameter{MICROSOFT_INT_REGS[slot], 0u, size, true};
            }
            return spill_to_stack();
        }

        case ABIConvention::ARM64_EABI: {
            if (fp_bank) {
                if (float_regs_used_ < ARM64_FLOAT_REGS.size()) {
                    const uint32_t reg = ARM64_FLOAT_REGS[float_regs_used_++];
                    return ABIParameter{reg, 0u, size, true};
                }
            } else {
                if (int_regs_used_ < ARM64_INT_REGS.size()) {
                    const uint32_t reg = ARM64_INT_REGS[int_regs_used_++];
                    return ABIParameter{reg, 0u, size, true};
                }
            }
            return spill_to_stack();
        }

        case ABIConvention::WebAssembly:
        default:
            // Keep lowering deterministic for non-native targets.
            return spill_to_stack();
    }
}

inline ABIParameter ABI_Lowering::lower_variadic_parameter(ABIValueClass val_class,
                                                           uint32_t size,
                                                           uint32_t alignment) noexcept {
    // Conservative and portable policy: keep variadic extras in memory for
    // deterministic handling across call boundaries and JIT/AOT parity.
    (void)val_class;
    if (size == 0) {
        size = 8;
    }
    if (alignment == 0 || (alignment & (alignment - 1u)) != 0u) {
        alignment = 8;
    }

    align_stack(alignment);
    const uint32_t offset = stack_offset_;
    const uint32_t rounded = (size + 7u) & ~7u;
    stack_offset_ += rounded;
    return ABIParameter{0xFFFFFFFFu, offset, size, false};
}

inline ABICallLayout ABI_Lowering::plan_call_layout(const std::vector<ABIValueClass>& value_classes,
                                                    const std::vector<uint32_t>& sizes,
                                                    ABIValueClass return_class,
                                                    bool is_variadic,
                                                    size_t fixed_parameter_count) noexcept {
    ABICallLayout layout;
    reset();
    const size_t n = std::min(value_classes.size(), sizes.size());
    layout.parameters.reserve(n);
    layout.uses_variadic_policy = is_variadic;

    for (size_t i = 0; i < n; ++i) {
        const ABIValueClass cls = value_classes[i];
        const uint32_t arg_size = sizes[i] == 0 ? 8u : sizes[i];

        if (is_variadic && i >= fixed_parameter_count) {
            layout.parameters.push_back(lower_variadic_parameter(cls, arg_size));
        } else {
            layout.parameters.push_back(lower_parameter(cls, arg_size));
        }
    }

    layout.return_register = get_return_register(return_class);
    layout.argument_stack_size = finalize_argument_stack_size();
    return layout;
}

inline uint32_t ABI_Lowering::get_return_register(ABIValueClass val_class) const noexcept {
    switch (convention_) {
        case ABIConvention::SystemV_x64:
        case ABIConvention::Microsoft_x64:
            if (val_class == ABIValueClass::FloatingPoint || val_class == ABIValueClass::Vector) {
                return 16u; // XMM0
            }
            if (val_class == ABIValueClass::Memory) {
                return 0xFFFFFFFFu;
            }
            return 0u; // RAX

        case ABIConvention::ARM64_EABI:
            if (val_class == ABIValueClass::FloatingPoint || val_class == ABIValueClass::Vector) {
                return 32u; // V0
            }
            if (val_class == ABIValueClass::Memory) {
                return 0xFFFFFFFFu;
            }
            return 0u; // X0

        case ABIConvention::WebAssembly:
        default:
            // Virtual return slot for VM stack-machine backends.
            return 0u;
    }
}

} // namespace dgtl::backend::lowering

// ================================
// COMPATIBILITY ALIAS (dgtl::isa)
// ================================
namespace dgtl::isa {
    using ABIConvention = dgtl::backend::lowering::ABIConvention;
    using ABIValueClass = dgtl::backend::lowering::ABIValueClass;
    using ABIParameter = dgtl::backend::lowering::ABIParameter;
    using ABI_Lowering = dgtl::backend::lowering::ABI_Lowering;
}
