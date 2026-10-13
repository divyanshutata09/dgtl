/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Component:   Deterministic Calling Convention & Hardware ABI Lowering Engine
 * Standards:   C++26 / Clang 18+ / MSVC 19.40+ / GCC 14+
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

module;

#include <cstdlib>
#include <cstdint>
#include <vector>
#include <array>
#include <utility>
#include <algorithm>
#include <span>

export module dgtl.mir.abi_lowering;

export namespace dgtl::mir::abi {

/**
 * @enum ABIConvention
 * @brief Target calling convention/ABI specification.
 */
enum class ABIConvention : uint8_t {
    SystemV_x64 = 0,    ///< System V AMD64 ABI (Linux/BSD/macOS standard)
    Microsoft_x64 = 1,  ///< Microsoft x64 calling convention (Windows)
    ARM64_EABI = 2,     ///< ARM64 EABI (AArch64 / Apple Silicon)
    WebAssembly = 3,    ///< WebAssembly (wasm32/wasm64)
    RISCV64_LP64D = 4,  ///< RISC-V 64 LP64D ABI
    ARM32_AAPCS = 5,    ///< ARM32 AAPCS / EABI
    CUDA_PTX = 6,       ///< CUDA PTX device / kernel parameter ABI
    ROCM_AMDGCN = 7,    ///< ROCm AMDGPU ABI (SGPR/VGPR binding)
    FPGA_Streaming = 8  ///< FPGA streaming channel handshake ABI
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
    uint32_t register_id = 0xFFFFFFFFu;      ///< Primary hardware register ID (if in_register=true)
    uint32_t register_id_high = 0xFFFFFFFFu; ///< Secondary hardware register ID (for 128-bit/split types)
    uint32_t stack_offset = 0;               ///< Byte offset from frame pointer (if in_register=false)
    uint32_t size = 0;                       ///< Parameter size in bytes
    bool in_register = false;                ///< true = bound to register, false = spilled to stack
    bool is_split = false;                   ///< true = split across primary and secondary registers

    [[nodiscard]] constexpr bool is_in_register() const noexcept { return in_register; }
    [[nodiscard]] constexpr bool is_on_stack() const noexcept { return !in_register; }
    [[nodiscard]] constexpr bool is_split_register() const noexcept { return is_split; }
    [[nodiscard]] constexpr uint32_t get_register_id() const noexcept { return register_id; }
    [[nodiscard]] constexpr uint32_t get_register_id_high() const noexcept { return register_id_high; }
    [[nodiscard]] constexpr uint32_t get_stack_offset() const noexcept { return stack_offset; }
    [[nodiscard]] constexpr uint32_t get_caller_stack_offset() const noexcept { return stack_offset; }
    [[nodiscard]] constexpr uint32_t get_callee_stack_offset() const noexcept { return stack_offset + 16u; }
};

struct ABICallLayout {
    std::vector<ABIParameter> parameters;
    uint32_t return_register = 0xFFFFFFFFu;
    uint32_t return_register_high = 0xFFFFFFFFu; ///< Secondary return register (e.g. RDX, X1, V1)
    uint32_t argument_stack_size = 0;
    bool uses_variadic_policy = false;

    [[nodiscard]] constexpr bool has_high_return_register() const noexcept {
        return return_register_high != 0xFFFFFFFFu;
    }
};

/**
 * @class ABI_Lowering
 * @brief Converts high-level parameters to ABI-conformant register/stack layout.
 * Implements calling conventions with strict hardware register assignments,
 * handling shadow spaces, split register banks, and struct alignment.
 */
class ABI_Lowering {
public:
    explicit ABI_Lowering(ABIConvention convention = ABIConvention::SystemV_x64) noexcept
        : convention_(convention) {
        reset();
    }

    ~ABI_Lowering() = default;

    ABI_Lowering(const ABI_Lowering&) = delete;
    ABI_Lowering& operator=(const ABI_Lowering&) = delete;

    [[nodiscard]] ABIParameter lower_parameter(ABIValueClass val_class, 
                                               uint32_t size, 
                                               uint32_t alignment = 8) noexcept;

    [[nodiscard]] ABIParameter lower_aggregate_parameter(std::span<const ABIValueClass> member_classes,
                                                         uint32_t total_size,
                                                         uint32_t alignment = 8) noexcept;

    [[nodiscard]] ABIParameter lower_variadic_parameter(ABIValueClass val_class,
                                                        uint32_t size,
                                                        uint32_t alignment = 8) noexcept;

    [[nodiscard]] ABICallLayout plan_call_layout(const std::vector<ABIValueClass>& value_classes,
                                                 const std::vector<uint32_t>& sizes,
                                                 ABIValueClass return_class,
                                                 bool is_variadic = false,
                                                 size_t fixed_parameter_count = 0,
                                                 uint32_t return_size = 8) noexcept;

    [[nodiscard]] std::pair<uint32_t, uint32_t> get_return_registers(ABIValueClass val_class,
                                                                     uint32_t size = 8) const noexcept;

    [[nodiscard]] uint32_t get_return_register(ABIValueClass val_class) const noexcept;

    [[nodiscard]] constexpr uint32_t get_stack_pointer_register() const noexcept {
        switch (convention_) {
            case ABIConvention::ARM64_EABI:     return 31;  /* SP (X31) */
            case ABIConvention::RISCV64_LP64D:  return 2;   /* sp (x2) */
            case ABIConvention::ARM32_AAPCS:    return 13;  /* sp (r13) */
            case ABIConvention::CUDA_PTX:       return 0;   /* %SP virtual pointer */
            case ABIConvention::ROCM_AMDGCN:    return 288; /* s32 scratch wave offset */
            case ABIConvention::FPGA_Streaming: return 0;   /* No hardware stack */
            case ABIConvention::WebAssembly:    return 0;   /* Virtual linear stack pointer */
            default:                            return 4;   /* RSP */
        }
    }

    [[nodiscard]] constexpr uint32_t get_frame_pointer_register() const noexcept {
        switch (convention_) {
            case ABIConvention::ARM64_EABI:     return 29;  /* X29 */
            case ABIConvention::RISCV64_LP64D:  return 8;   /* s0/fp (x8) */
            case ABIConvention::ARM32_AAPCS:    return 11;  /* r11/fp */
            case ABIConvention::CUDA_PTX:       return 0;   /* %FP virtual pointer */
            case ABIConvention::ROCM_AMDGCN:    return 289; /* s33 frame pointer */
            case ABIConvention::FPGA_Streaming: return 0;   /* No frame pointer */
            case ABIConvention::WebAssembly:    return 0;   /* Virtual frame pointer */
            default:                            return 5;   /* RBP */
        }
    }

    void reset() noexcept {
        int_regs_used_ = 0;
        float_regs_used_ = 0;
        unified_slots_used_ = 0;
        stack_offset_ = (convention_ == ABIConvention::Microsoft_x64) ? 32 : 0;
    }

    [[nodiscard]] constexpr ABIConvention get_convention() const noexcept { 
        return convention_; 
    }

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
    
    uint8_t int_regs_used_ = 0;
    uint8_t float_regs_used_ = 0;
    uint8_t unified_slots_used_ = 0;
    uint32_t stack_offset_ = 0;

    void align_stack(uint32_t alignment) noexcept {
        if (alignment > 1) [[likely]] {
            stack_offset_ = (stack_offset_ + alignment - 1) & ~(alignment - 1);
        }
    }

    // Hardware Register Banks
    static constexpr std::array<uint32_t, 6> SYSTEMV_INT_REGS = { 7, 6, 2, 1, 8, 9 };
    static constexpr std::array<uint32_t, 8> SYSTEMV_FLOAT_REGS = { 16, 17, 18, 19, 20, 21, 22, 23 };

    static constexpr std::array<uint32_t, 4> MICROSOFT_INT_REGS = { 1, 2, 8, 9 };
    static constexpr std::array<uint32_t, 4> MICROSOFT_FLOAT_REGS = { 16, 17, 18, 19 };

    static constexpr std::array<uint32_t, 8> ARM64_INT_REGS = { 0, 1, 2, 3, 4, 5, 6, 7 };
    static constexpr std::array<uint32_t, 8> ARM64_FLOAT_REGS = { 32, 33, 34, 35, 36, 37, 38, 39 };

    static constexpr std::array<uint32_t, 8> RISCV64_INT_REGS = { 10, 11, 12, 13, 14, 15, 16, 17 };
    static constexpr std::array<uint32_t, 8> RISCV64_FLOAT_REGS = { 42, 43, 44, 45, 46, 47, 48, 49 };

    static constexpr std::array<uint32_t, 4> ARM32_INT_REGS = { 0, 1, 2, 3 };
    static constexpr std::array<uint32_t, 8> ARM32_FLOAT_REGS = { 16, 17, 18, 19, 20, 21, 22, 23 };

    static constexpr std::array<uint32_t, 8> CUDA_INT_REGS = { 0, 1, 2, 3, 4, 5, 6, 7 };
    static constexpr std::array<uint32_t, 8> CUDA_FLOAT_REGS = { 16, 17, 18, 19, 20, 21, 22, 23 };

    static constexpr std::array<uint32_t, 8> ROCM_INT_REGS = { 256, 257, 258, 259, 260, 261, 262, 263 };
    static constexpr std::array<uint32_t, 8> ROCM_FLOAT_REGS = { 0, 1, 2, 3, 4, 5, 6, 7 };

    static constexpr std::array<uint32_t, 8> FPGA_STREAM_REGS = { 0, 1, 2, 3, 4, 5, 6, 7 };
};

inline ABIParameter ABI_Lowering::lower_parameter(ABIValueClass val_class,
                                                  uint32_t size,
                                                  uint32_t alignment) noexcept {
    if (size == 0) [[unlikely]] size = 8;
    if (alignment == 0 || (alignment & (alignment - 1u)) != 0u) [[unlikely]] alignment = 8;

    const auto spill_to_stack = [&]() noexcept {
        align_stack(alignment);
        const uint32_t offset = stack_offset_;
        const uint32_t rounded = (size + 7u) & ~7u;
        stack_offset_ += rounded;
        return ABIParameter{0xFFFFFFFFu, 0xFFFFFFFFu, offset, size, false, false};
    };

    if (val_class == ABIValueClass::Memory || size > 16u) [[unlikely]] {
        return spill_to_stack();
    }

    const bool fp_bank = (val_class == ABIValueClass::FloatingPoint ||
                          val_class == ABIValueClass::Vector);

    switch (convention_) {
        case ABIConvention::SystemV_x64: {
            if (fp_bank) {
                if (float_regs_used_ < SYSTEMV_FLOAT_REGS.size()) [[likely]] {
                    const uint32_t reg = SYSTEMV_FLOAT_REGS[float_regs_used_++];
                    return ABIParameter{reg, 0xFFFFFFFFu, 0u, size, true, false};
                }
            } else {
                if (int_regs_used_ < SYSTEMV_INT_REGS.size()) [[likely]] {
                    const uint32_t reg = SYSTEMV_INT_REGS[int_regs_used_++];
                    return ABIParameter{reg, 0xFFFFFFFFu, 0u, size, true, false};
                }
            }
            return spill_to_stack();
        }

        case ABIConvention::Microsoft_x64: {
            if (unified_slots_used_ < 4u) [[likely]] {
                const uint8_t slot = unified_slots_used_++;
                if (fp_bank) {
                    return ABIParameter{MICROSOFT_FLOAT_REGS[slot], 0xFFFFFFFFu, 0u, size, true, false};
                }
                return ABIParameter{MICROSOFT_INT_REGS[slot], 0xFFFFFFFFu, 0u, size, true, false};
            }
            return spill_to_stack();
        }

        case ABIConvention::ARM64_EABI: {
            if (fp_bank) {
                if (float_regs_used_ < ARM64_FLOAT_REGS.size()) [[likely]] {
                    const uint32_t reg = ARM64_FLOAT_REGS[float_regs_used_++];
                    return ABIParameter{reg, 0xFFFFFFFFu, 0u, size, true, false};
                }
            } else {
                if (int_regs_used_ < ARM64_INT_REGS.size()) [[likely]] {
                    const uint32_t reg = ARM64_INT_REGS[int_regs_used_++];
                    return ABIParameter{reg, 0xFFFFFFFFu, 0u, size, true, false};
                }
            }
            return spill_to_stack();
        }

        case ABIConvention::WebAssembly:
            if (fp_bank) {
                const uint32_t reg = 32u + float_regs_used_++;
                return ABIParameter{reg, 0xFFFFFFFFu, 0u, size, true, false};
            }
            return ABIParameter{int_regs_used_++, 0xFFFFFFFFu, 0u, size, true, false};

        case ABIConvention::RISCV64_LP64D: {
            if (fp_bank) {
                if (float_regs_used_ < RISCV64_FLOAT_REGS.size()) [[likely]] {
                    const uint32_t reg = RISCV64_FLOAT_REGS[float_regs_used_++];
                    return ABIParameter{reg, 0xFFFFFFFFu, 0u, size, true, false};
                }
            } else {
                if (int_regs_used_ < RISCV64_INT_REGS.size()) [[likely]] {
                    const uint32_t reg = RISCV64_INT_REGS[int_regs_used_++];
                    return ABIParameter{reg, 0xFFFFFFFFu, 0u, size, true, false};
                }
            }
            return spill_to_stack();
        }

        case ABIConvention::ARM32_AAPCS: {
            if (fp_bank) {
                if (float_regs_used_ < ARM32_FLOAT_REGS.size()) [[likely]] {
                    const uint32_t reg = ARM32_FLOAT_REGS[float_regs_used_++];
                    return ABIParameter{reg, 0xFFFFFFFFu, 0u, size, true, false};
                }
            } else {
                if (int_regs_used_ < ARM32_INT_REGS.size()) [[likely]] {
                    const uint32_t reg = ARM32_INT_REGS[int_regs_used_++];
                    return ABIParameter{reg, 0xFFFFFFFFu, 0u, size, true, false};
                }
            }
            return spill_to_stack();
        }

        case ABIConvention::CUDA_PTX: {
            if (fp_bank) {
                if (float_regs_used_ < CUDA_FLOAT_REGS.size()) [[likely]] {
                    const uint32_t reg = CUDA_FLOAT_REGS[float_regs_used_++];
                    return ABIParameter{reg, 0xFFFFFFFFu, 0u, size, true, false};
                }
            } else {
                if (int_regs_used_ < CUDA_INT_REGS.size()) [[likely]] {
                    const uint32_t reg = CUDA_INT_REGS[int_regs_used_++];
                    return ABIParameter{reg, 0xFFFFFFFFu, 0u, size, true, false};
                }
            }
            return spill_to_stack();
        }

        case ABIConvention::ROCM_AMDGCN: {
            if (fp_bank) {
                if (float_regs_used_ < ROCM_FLOAT_REGS.size()) [[likely]] {
                    const uint32_t reg = ROCM_FLOAT_REGS[float_regs_used_++];
                    return ABIParameter{reg, 0xFFFFFFFFu, 0u, size, true, false};
                }
            } else {
                if (int_regs_used_ < ROCM_INT_REGS.size()) [[likely]] {
                    const uint32_t reg = ROCM_INT_REGS[int_regs_used_++];
                    return ABIParameter{reg, 0xFFFFFFFFu, 0u, size, true, false};
                }
            }
            return spill_to_stack();
        }

        case ABIConvention::FPGA_Streaming: {
            if (int_regs_used_ < FPGA_STREAM_REGS.size()) [[likely]] {
                const uint32_t reg = FPGA_STREAM_REGS[int_regs_used_++];
                return ABIParameter{reg, 0xFFFFFFFFu, 0u, size, true, false};
            }
            return spill_to_stack();
        }

        default:
            return spill_to_stack();
    }
}

inline ABIParameter ABI_Lowering::lower_aggregate_parameter(std::span<const ABIValueClass> member_classes,
                                                            uint32_t total_size,
                                                            uint32_t alignment) noexcept {
    if (total_size == 0) total_size = 8;
    if (alignment == 0 || (alignment & (alignment - 1u)) != 0u) alignment = 8;

    const auto spill_to_stack = [&]() noexcept {
        align_stack(alignment);
        const uint32_t offset = stack_offset_;
        const uint32_t rounded = (total_size + 7u) & ~7u;
        stack_offset_ += rounded;
        return ABIParameter{0xFFFFFFFFu, 0xFFFFFFFFu, offset, total_size, false, false};
    };

    if (total_size > 16u || member_classes.empty()) {
        return spill_to_stack();
    }

    switch (convention_) {
        case ABIConvention::SystemV_x64: {
            if (total_size <= 8u) {
                const bool is_fp = (member_classes[0] == ABIValueClass::FloatingPoint ||
                                    member_classes[0] == ABIValueClass::Vector);
                if (is_fp && float_regs_used_ < SYSTEMV_FLOAT_REGS.size()) {
                    return ABIParameter{SYSTEMV_FLOAT_REGS[float_regs_used_++], 0xFFFFFFFFu, 0u, total_size, true, false};
                } else if (!is_fp && int_regs_used_ < SYSTEMV_INT_REGS.size()) {
                    return ABIParameter{SYSTEMV_INT_REGS[int_regs_used_++], 0xFFFFFFFFu, 0u, total_size, true, false};
                }
                return spill_to_stack();
            } else {
                const bool chunk0_fp = (member_classes[0] == ABIValueClass::FloatingPoint || member_classes[0] == ABIValueClass::Vector);
                const bool chunk1_fp = (member_classes.size() > 1 && (member_classes[1] == ABIValueClass::FloatingPoint || member_classes[1] == ABIValueClass::Vector));

                const size_t req_fp = (chunk0_fp ? 1 : 0) + (chunk1_fp ? 1 : 0);
                const size_t req_int = (chunk0_fp ? 0 : 1) + (chunk1_fp ? 0 : 1);

                if (float_regs_used_ + req_fp <= SYSTEMV_FLOAT_REGS.size() &&
                    int_regs_used_ + req_int <= SYSTEMV_INT_REGS.size()) {
                    const uint32_t reg0 = chunk0_fp ? SYSTEMV_FLOAT_REGS[float_regs_used_++] : SYSTEMV_INT_REGS[int_regs_used_++];
                    const uint32_t reg1 = chunk1_fp ? SYSTEMV_FLOAT_REGS[float_regs_used_++] : SYSTEMV_INT_REGS[int_regs_used_++];
                    return ABIParameter{reg0, reg1, 0u, total_size, true, true};
                }
                return spill_to_stack();
            }
        }

        case ABIConvention::Microsoft_x64: {
            if (total_size <= 8u) {
                if (unified_slots_used_ < 4u) {
                    const uint8_t slot = unified_slots_used_++;
                    return ABIParameter{MICROSOFT_INT_REGS[slot], 0xFFFFFFFFu, 0u, total_size, true, false};
                }
            }
            return spill_to_stack();
        }

        case ABIConvention::ARM64_EABI: {
            bool is_hfa = true;
            for (auto mc : member_classes) {
                if (mc != ABIValueClass::FloatingPoint && mc != ABIValueClass::Vector) {
                    is_hfa = false;
                    break;
                }
            }
            if (is_hfa && member_classes.size() <= 4) {
                if (float_regs_used_ + member_classes.size() <= ARM64_FLOAT_REGS.size()) {
                    const uint32_t primary = ARM64_FLOAT_REGS[float_regs_used_++];
                    uint32_t secondary = 0xFFFFFFFFu;
                    if (member_classes.size() > 1) {
                        secondary = ARM64_FLOAT_REGS[float_regs_used_++];
                    }
                    for (size_t k = 2; k < member_classes.size(); ++k) {
                        ++float_regs_used_;
                    }
                    return ABIParameter{primary, secondary, 0u, total_size, true, member_classes.size() > 1};
                }
            } else if (total_size <= 16u) {
                const size_t num_gprs = (total_size + 7u) / 8u;
                if (int_regs_used_ + num_gprs <= ARM64_INT_REGS.size()) {
                    const uint32_t primary = ARM64_INT_REGS[int_regs_used_++];
                    const uint32_t secondary = (num_gprs > 1) ? ARM64_INT_REGS[int_regs_used_++] : 0xFFFFFFFFu;
                    return ABIParameter{primary, secondary, 0u, total_size, true, num_gprs > 1};
                }
            }
            return spill_to_stack();
        }

        case ABIConvention::RISCV64_LP64D: {
            bool is_fp_struct = true;
            for (auto mc : member_classes) {
                if (mc != ABIValueClass::FloatingPoint && mc != ABIValueClass::Vector) {
                    is_fp_struct = false;
                    break;
                }
            }
            if (is_fp_struct && float_regs_used_ + member_classes.size() <= RISCV64_FLOAT_REGS.size()) {
                const uint32_t primary = RISCV64_FLOAT_REGS[float_regs_used_++];
                const uint32_t secondary = (member_classes.size() > 1) ? RISCV64_FLOAT_REGS[float_regs_used_++] : 0xFFFFFFFFu;
                return ABIParameter{primary, secondary, 0u, total_size, true, member_classes.size() > 1};
            }
            const size_t num_regs = (total_size + 7u) / 8u;
            if (int_regs_used_ + num_regs <= RISCV64_INT_REGS.size()) {
                const uint32_t primary = RISCV64_INT_REGS[int_regs_used_++];
                const uint32_t secondary = (num_regs > 1) ? RISCV64_INT_REGS[int_regs_used_++] : 0xFFFFFFFFu;
                return ABIParameter{primary, secondary, 0u, total_size, true, num_regs > 1};
            }
            return spill_to_stack();
        }

        case ABIConvention::ARM32_AAPCS: {
            if (total_size <= 8u) {
                const size_t num_regs = (total_size + 3u) / 4u;
                if (int_regs_used_ + num_regs <= ARM32_INT_REGS.size()) {
                    const uint32_t primary = ARM32_INT_REGS[int_regs_used_++];
                    const uint32_t secondary = (num_regs > 1) ? ARM32_INT_REGS[int_regs_used_++] : 0xFFFFFFFFu;
                    return ABIParameter{primary, secondary, 0u, total_size, true, num_regs > 1};
                }
            }
            return spill_to_stack();
        }

        case ABIConvention::CUDA_PTX: {
            const size_t num_regs = (total_size + 7u) / 8u;
            if (int_regs_used_ + num_regs <= CUDA_INT_REGS.size()) {
                const uint32_t primary = CUDA_INT_REGS[int_regs_used_++];
                const uint32_t secondary = (num_regs > 1) ? CUDA_INT_REGS[int_regs_used_++] : 0xFFFFFFFFu;
                return ABIParameter{primary, secondary, 0u, total_size, true, num_regs > 1};
            }
            return spill_to_stack();
        }

        case ABIConvention::ROCM_AMDGCN: {
            const size_t num_regs = (total_size + 7u) / 8u;
            if (int_regs_used_ + num_regs <= ROCM_INT_REGS.size()) {
                const uint32_t primary = ROCM_INT_REGS[int_regs_used_++];
                const uint32_t secondary = (num_regs > 1) ? ROCM_INT_REGS[int_regs_used_++] : 0xFFFFFFFFu;
                return ABIParameter{primary, secondary, 0u, total_size, true, num_regs > 1};
            }
            return spill_to_stack();
        }

        case ABIConvention::FPGA_Streaming: {
            const size_t num_regs = (total_size + 7u) / 8u;
            if (int_regs_used_ + num_regs <= FPGA_STREAM_REGS.size()) {
                const uint32_t primary = FPGA_STREAM_REGS[int_regs_used_++];
                const uint32_t secondary = (num_regs > 1) ? FPGA_STREAM_REGS[int_regs_used_++] : 0xFFFFFFFFu;
                return ABIParameter{primary, secondary, 0u, total_size, true, num_regs > 1};
            }
            return spill_to_stack();
        }

        default:
            return spill_to_stack();
    }
}

inline ABIParameter ABI_Lowering::lower_variadic_parameter(ABIValueClass val_class,
                                                           uint32_t size,
                                                           uint32_t alignment) noexcept {
    (void)val_class;
    if (size == 0) [[unlikely]] size = 8;
    if (alignment == 0 || (alignment & (alignment - 1u)) != 0u) [[unlikely]] alignment = 8;

    align_stack(alignment);
    const uint32_t offset = stack_offset_;
    const uint32_t rounded = (size + 7u) & ~7u;
    stack_offset_ += rounded;
    return ABIParameter{0xFFFFFFFFu, 0xFFFFFFFFu, offset, size, false, false};
}

inline ABICallLayout ABI_Lowering::plan_call_layout(const std::vector<ABIValueClass>& value_classes,
                                                    const std::vector<uint32_t>& sizes,
                                                    ABIValueClass return_class,
                                                    bool is_variadic,
                                                    size_t fixed_parameter_count,
                                                    uint32_t return_size) noexcept {
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

    const auto [ret_lo, ret_hi] = get_return_registers(return_class, return_size);
    layout.return_register = ret_lo;
    layout.return_register_high = ret_hi;
    layout.argument_stack_size = finalize_argument_stack_size();
    return layout;
}

inline std::pair<uint32_t, uint32_t> ABI_Lowering::get_return_registers(ABIValueClass val_class,
                                                                         uint32_t size) const noexcept {
    switch (convention_) {
        case ABIConvention::SystemV_x64:
            if (val_class == ABIValueClass::FloatingPoint || val_class == ABIValueClass::Vector) {
                if (size > 8u && size <= 16u) {
                    return {16u, 17u}; // XMM0:XMM1
                }
                return {16u, 0xFFFFFFFFu}; // XMM0
            }
            if (val_class == ABIValueClass::Memory) {
                return {0xFFFFFFFFu, 0xFFFFFFFFu};
            }
            if (size > 8u && size <= 16u) {
                return {0u, 2u}; // RAX:RDX
            }
            return {0u, 0xFFFFFFFFu}; // RAX

        case ABIConvention::Microsoft_x64:
            if (val_class == ABIValueClass::FloatingPoint || val_class == ABIValueClass::Vector) {
                return {16u, 0xFFFFFFFFu}; // XMM0
            }
            if (val_class == ABIValueClass::Memory) {
                return {0xFFFFFFFFu, 0xFFFFFFFFu};
            }
            if (size > 8u && size <= 16u) {
                return {0u, 2u}; // RAX:RDX
            }
            return {0u, 0xFFFFFFFFu}; // RAX

        case ABIConvention::ARM64_EABI:
            if (val_class == ABIValueClass::FloatingPoint || val_class == ABIValueClass::Vector) {
                if (size > 8u && size <= 16u) {
                    return {32u, 33u}; // V0:V1
                }
                return {32u, 0xFFFFFFFFu}; // V0
            }
            if (val_class == ABIValueClass::Memory) {
                return {0xFFFFFFFFu, 0xFFFFFFFFu};
            }
            if (size > 8u && size <= 16u) {
                return {0u, 1u}; // X0:X1
            }
            return {0u, 0xFFFFFFFFu}; // X0

        case ABIConvention::RISCV64_LP64D:
            if (val_class == ABIValueClass::FloatingPoint || val_class == ABIValueClass::Vector) {
                if (size > 8u && size <= 16u) {
                    return {42u, 43u}; // fa0:fa1
                }
                return {42u, 0xFFFFFFFFu}; // fa0
            }
            if (val_class == ABIValueClass::Memory) {
                return {0xFFFFFFFFu, 0xFFFFFFFFu};
            }
            if (size > 8u && size <= 16u) {
                return {10u, 11u}; // a0:a1
            }
            return {10u, 0xFFFFFFFFu}; // a0

        case ABIConvention::ARM32_AAPCS:
            if (val_class == ABIValueClass::FloatingPoint || val_class == ABIValueClass::Vector) {
                if (size > 8u && size <= 16u) {
                    return {16u, 17u}; // s0:s1
                }
                return {16u, 0xFFFFFFFFu}; // s0
            }
            if (val_class == ABIValueClass::Memory) {
                return {0xFFFFFFFFu, 0xFFFFFFFFu};
            }
            if (size > 4u && size <= 8u) {
                return {0u, 1u}; // r0:r1
            }
            return {0u, 0xFFFFFFFFu}; // r0

        case ABIConvention::CUDA_PTX:
            if (val_class == ABIValueClass::FloatingPoint || val_class == ABIValueClass::Vector) {
                if (size > 8u && size <= 16u) {
                    return {16u, 17u}; // %f0:%f1
                }
                return {16u, 0xFFFFFFFFu}; // %f0
            }
            if (val_class == ABIValueClass::Memory) {
                return {0xFFFFFFFFu, 0xFFFFFFFFu};
            }
            if (size > 8u && size <= 16u) {
                return {0u, 1u}; // %r0:%r1
            }
            return {0u, 0xFFFFFFFFu}; // %r0

        case ABIConvention::ROCM_AMDGCN:
            if (val_class == ABIValueClass::FloatingPoint || val_class == ABIValueClass::Vector) {
                if (size > 8u && size <= 16u) {
                    return {0u, 1u}; // v0:v1
                }
                return {0u, 0xFFFFFFFFu}; // v0
            }
            if (val_class == ABIValueClass::Memory) {
                return {0xFFFFFFFFu, 0xFFFFFFFFu};
            }
            if (size > 8u && size <= 16u) {
                return {256u, 257u}; // s0:s1
            }
            return {256u, 0xFFFFFFFFu}; // s0

        case ABIConvention::FPGA_Streaming:
            if (val_class == ABIValueClass::Memory) {
                return {0xFFFFFFFFu, 0xFFFFFFFFu};
            }
            if (size > 8u && size <= 16u) {
                return {0u, 1u}; // Output stream ports 0:1
            }
            return {0u, 0xFFFFFFFFu}; // Output stream port 0

        case ABIConvention::WebAssembly:
        default:
            return {0u, 0xFFFFFFFFu};
    }
}
inline uint32_t ABI_Lowering::get_return_register(ABIValueClass val_class) const noexcept {
    return get_return_registers(val_class, 8u).first;
}

} // namespace dgtl::mir::abi

export namespace dgtl::mir {
    using abi::ABICallLayout;
    using abi::ABIConvention;
    using abi::ABIValueClass;
    using abi::ABIParameter;
    using abi::ABI_Lowering;
}