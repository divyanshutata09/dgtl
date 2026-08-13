/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

/* * DGTL (Deep General-purpose Transformation Layer) version 1.0.0 : THE COMPILER SPEC
 * STEP 2: BIT-PACKED INTERMEDIATE REPRESENTATION (BIR) - VERSION 2.1
 * PILLAR 4: HARDWARE AFFINITY & BACKEND EMISSION CONSTRAINTS
 * TARGET ENGINE: ISA Descriptors, Register Sets, & Micro-Arch Invariants
 * STANDARDS: C++26 / UCRT64 (Compile-Time Hardware Specialization)
 */

module;

#include <cstdlib>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <span>
#include <chrono>
#include <numeric>

#if defined(__linux__)
#   include <sched.h>
#   include <unistd.h>
#endif

#ifdef _WIN32
#   ifndef WIN32_LEAN_AND_MEAN
#       define WIN32_LEAN_AND_MEAN
#   endif
#   include <windows.h>
#endif

#if defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64)
#   if defined(_MSC_VER)
#       include <intrin.h>
#   else
#       include <x86intrin.h>
#   endif
#endif

#ifdef CONST
#undef CONST
#endif
#ifdef INVALID_ATOM
#undef INVALID_ATOM
#endif
#ifdef RELEASE
#undef RELEASE
#endif
#ifdef MEM_RELEASE
#undef MEM_RELEASE
#endif
#ifdef OPAQUE
#undef OPAQUE
#endif
#ifdef MOV
#undef MOV
#endif
#ifdef REGISTER
#undef REGISTER
#endif

export module dgtl.target;

import dgtl.ir_enums;

#if defined(__GNUC__) || defined(__clang__)
#   define DGTL_HW_FORCEINLINE inline __attribute__((always_inline))
#   define DGTL_HW_AVX2_TARGET __attribute__((target("avx2")))
#elif defined(_MSC_VER)
#   define DGTL_HW_FORCEINLINE __forceinline
#   define DGTL_HW_AVX2_TARGET
#else
#   define DGTL_HW_FORCEINLINE inline
#   define DGTL_HW_AVX2_TARGET
#endif

#if defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64)
#   define DGTL_HAS_X86 1
#else
#   define DGTL_HAS_X86 0
#endif


export namespace dgtl::core {

using OpCode = dgtl::ir::OpCode;

// ============================================================
// Architecture & SIMD Enums (Merged from both files)
// ============================================================

enum class ArchType : uint8_t {
    X86_64_AVX512,  // Tier-1: HFT / AI Infrastructure (Primary)
    X86_64_AVX2,    // Tier-1b: Maximum compatibility on x86 hosts
    AARCH64_SVE,    // Tier-2: Mobile/Edge Performance
    FPGA_VERILOG,   // Low-Latency Custom Silicon
    WASM_SIMD       // High-Performance Web
};

enum class SimdTier : uint8_t {
    Scalar = 0,  // No SIMD or non-x86
    AVX2   = 1,  // 256-bit YMM dual-pump
    AVX512 = 2   // 512-bit ZMM (requires avx512f + avx512dq)
};

enum class SimdExecutionProfile : uint8_t {
    Scalar = 0,
    AVX2DualPump = 1,
    AVX512Wide = 2
};

// ============================================================
// Feature Detection Structures
// ============================================================

struct X86SimdCaps {
    bool has_avx2 = false;
    bool has_avx512 = false;
    bool has_fma = false;
    bool has_bmi2 = false;
    bool has_tsc = false;
};

/**
 * @struct HardwareFeatures
 * @brief Consolidated feature set for internal state.
 */
struct HardwareFeatures {
    SimdTier simd      = SimdTier::Scalar;
    bool     has_tsc   = false;
    bool     has_avx2  = false;
    bool     has_avx512 = false;
    bool     has_fma   = false;
    bool     has_bmi2  = false;
    bool     has_amx   = false;
    uint32_t cache_line_size = 64;
    uint64_t preferred_affinity_mask = 0x4ull;
    uint32_t preferred_core_index = 2;
};

struct AffinityPolicy {
    uint64_t mask = 0x4ull;
    uint32_t preferred_core = 2;
    bool realtime_priority = true;
};

[[nodiscard]] AffinityPolicy default_high_performance_affinity() noexcept;
[[nodiscard]] bool apply_high_performance_affinity(const AffinityPolicy& policy = default_high_performance_affinity()) noexcept;

// ============================================================
// Feature Detection (Forward Declarations)
// ============================================================
// GCC 15 ICE workaround (cp/module.cc:9996):
// __builtin_cpu_supports(), __rdtsc(), and AVX intrinsics inside *inline*
// exported functions trigger an ICE because GCC's module serializer cannot
// round-trip the underlying compiler-builtin/ifunc tree nodes through the
// BMI.  All functions containing those calls are declared here but defined
// out-of-line in Target.cpp (the module implementation unit).
[[nodiscard]] std::uint64_t read_tsc() noexcept;
void stabilize() noexcept;
[[nodiscard]] X86SimdCaps detect_x86_simd_caps() noexcept;
[[nodiscard]] HardwareFeatures detect_hardware_impl() noexcept;

[[nodiscard]] inline HardwareFeatures detect_hardware() noexcept {
    return detect_hardware_impl();
}
void prime_preferred_simd_units(std::size_t iterations = 2048) noexcept;

[[nodiscard]] inline SimdExecutionProfile preferred_simd_execution_profile() noexcept {
    const HardwareFeatures hw = detect_hardware();
    if (hw.has_avx2) {
        return SimdExecutionProfile::AVX2DualPump;
    }
    if (hw.has_avx512) {
        return SimdExecutionProfile::AVX512Wide;
    }
    return SimdExecutionProfile::Scalar;
}

[[nodiscard]] inline SimdTier preferred_simd_tier() noexcept {
    switch (preferred_simd_execution_profile()) {
        case SimdExecutionProfile::AVX2DualPump:
            return SimdTier::AVX2;
        case SimdExecutionProfile::AVX512Wide:
            return SimdTier::AVX512;
        case SimdExecutionProfile::Scalar:
        default:
            return SimdTier::Scalar;
    }
}

[[nodiscard]] inline ArchType select_best_x86_arch() noexcept {
    const X86SimdCaps caps = detect_x86_simd_caps();
    return caps.has_avx512 ? ArchType::X86_64_AVX512 : ArchType::X86_64_AVX2;
}

[[nodiscard]] inline ArchType select_preferred_x86_arch() noexcept {
    return preferred_simd_tier() == SimdTier::AVX512 ? ArchType::X86_64_AVX512 : ArchType::X86_64_AVX2;
}
// prime_preferred_simd_units is declared above; defined out-of-line in Target.cpp



// ============================================================
// TargetMachine Class (Merged Architecture Awareness)
// ============================================================

struct RegisterDesc {
    uint32_t id = 0;
    [[maybe_unused]] std::string_view name{};
    uint32_t bit_width = 0;
    bool is_volatile = false;
    bool is_vector = false;
};

enum class RegisterBank : uint8_t {
    GeneralPurpose = 0,
    Vector = 1,
    Predicate = 2,
    Special = 3,
};

enum class CallingConventionKind : uint8_t {
    SystemV_x64 = 0,
    Windows_x64 = 1,
    AAPCS64 = 2,
    WasmCABI = 3,
    BareMetal = 4,
};

struct TargetRegisterInfo {
    uint16_t id = 0;
    [[maybe_unused]] std::string_view name{};
    RegisterBank bank = RegisterBank::GeneralPurpose;
    uint16_t bit_width = 0;
    bool caller_saved = false;
    bool allocatable = true;
};

struct CallingConventionDesc {
    CallingConventionKind kind = CallingConventionKind::BareMetal;
    std::string_view name{};
    std::array<uint16_t, 8> integer_arg_registers{};
    std::array<uint16_t, 8> vector_arg_registers{};
    uint8_t integer_arg_count = 0;
    uint8_t vector_arg_count = 0;
    uint16_t integer_return_register = 0;
    uint16_t vector_return_register = 0;
    uint16_t stack_alignment = 16;
    uint16_t shadow_space_bytes = 0;

    [[nodiscard]] std::string_view get_name() const noexcept { return name; }
    [[nodiscard]] uint16_t get_integer_arg_reg(size_t index) const noexcept {
        return (index < integer_arg_registers.size()) ? integer_arg_registers[index] : 0;
    }
    [[nodiscard]] uint16_t get_vector_arg_reg(size_t index) const noexcept {
        return (index < vector_arg_registers.size()) ? vector_arg_registers[index] : 0;
    }
};

struct InstructionDesc {
    OpCode opcode = static_cast<OpCode>(0);
    uint8_t latency_cycles = 1;
    uint8_t reciprocal_throughput = 1;
    uint16_t legal_scalar_width_mask = 0;
    bool supports_fusion = false;
};

class TargetDescription {
public:
    static constexpr uint16_t kWidth32 = 1u << 0;
    static constexpr uint16_t kWidth64 = 1u << 1;
    static constexpr uint16_t kWidth128 = 1u << 2;
    static constexpr uint16_t kWidth256 = 1u << 3;
    static constexpr uint16_t kWidth512 = 1u << 4;

    [[nodiscard]] static TargetDescription for_arch(ArchType arch);

    [[nodiscard]] ArchType arch() const noexcept { return arch_; }
    [[nodiscard]] std::span<const TargetRegisterInfo> general_purpose_registers() const noexcept {
        return std::span<const TargetRegisterInfo>(gp_registers_.data(), gp_register_count_);
    }
    [[nodiscard]] std::span<const TargetRegisterInfo> vector_registers() const noexcept {
        return std::span<const TargetRegisterInfo>(vector_registers_.data(), vector_register_count_);
    }
    [[nodiscard]] const CallingConventionDesc& default_calling_convention() const noexcept {
        return calling_convention_;
    }
    [[nodiscard]] std::optional<InstructionDesc> instruction_desc(OpCode opcode) const noexcept {
        const auto it = std::find_if(instruction_table_.begin(), instruction_table_.begin() + instruction_count_,
                                     [opcode](const auto& desc) { return desc.opcode == opcode; });
        if (it != instruction_table_.begin() + instruction_count_) {
            return *it;
        }
        return std::nullopt;
    }
    [[nodiscard]] uint8_t instruction_latency(OpCode opcode) const noexcept {
        const auto desc = instruction_desc(opcode);
        return desc.has_value() ? desc->latency_cycles : 1;
    }
    [[nodiscard]] uint8_t instruction_reciprocal_throughput(OpCode opcode) const noexcept {
        const auto desc = instruction_desc(opcode);
        return desc.has_value() ? desc->reciprocal_throughput : 1;
    }
    [[nodiscard]] bool instruction_supports_fusion(OpCode opcode) const noexcept {
        const auto desc = instruction_desc(opcode);
        return desc.has_value() && desc->supports_fusion;
    }
    [[nodiscard]] bool supports_scalar_width(uint32_t bits) const noexcept {
        const uint16_t mask = width_to_mask(bits);
        return (supported_scalar_widths_ & mask) != 0;
    }
    [[nodiscard]] bool supports_vector_width(uint32_t bits) const noexcept {
        const uint16_t mask = width_to_mask(bits);
        return (supported_vector_widths_ & mask) != 0;
    }
    [[nodiscard]] bool supports_fma() const noexcept {
        return supports_fma_;
    }
    [[nodiscard]] float average_instruction_latency() const noexcept {
        if (instruction_count_ == 0) return 1.0f;
        const uint32_t total = std::accumulate(instruction_table_.begin(), instruction_table_.begin() + instruction_count_, 0u,
                                               [](uint32_t sum, const auto& desc) { return sum + desc.latency_cycles; });
        return static_cast<float>(total) / static_cast<float>(instruction_count_);
    }
    [[nodiscard]] float average_instruction_reciprocal_throughput() const noexcept {
        if (instruction_count_ == 0) return 1.0f;
        const uint32_t total = std::accumulate(instruction_table_.begin(), instruction_table_.begin() + instruction_count_, 0u,
                                               [](uint32_t sum, const auto& desc) { return sum + desc.reciprocal_throughput; });
        return static_cast<float>(total) / static_cast<float>(instruction_count_);
    }

private:
    ArchType arch_ = ArchType::X86_64_AVX2;
    std::array<TargetRegisterInfo, 32> gp_registers_{};
    std::array<TargetRegisterInfo, 32> vector_registers_{};
    std::array<InstructionDesc, 32> instruction_table_{};
    CallingConventionDesc calling_convention_{};
    uint8_t gp_register_count_ = 0;
    uint8_t vector_register_count_ = 0;
    uint8_t instruction_count_ = 0;
    uint16_t supported_scalar_widths_ = kWidth64;
    uint16_t supported_vector_widths_ = 0;
    bool supports_fma_ = false;

    [[nodiscard]] static constexpr uint16_t width_to_mask(uint32_t bits) noexcept {
        switch (bits) {
            case 32: return kWidth32;
            case 64: return kWidth64;
            case 128: return kWidth128;
            case 256: return kWidth256;
            case 512: return kWidth512;
            default: return 0;
        }
    }

    void add_gp(uint16_t id, std::string_view name, bool caller_saved) noexcept {
        gp_registers_[gp_register_count_++] = TargetRegisterInfo{id, name, RegisterBank::GeneralPurpose, 64, caller_saved, true};
    }

    void add_vec(uint16_t id, std::string_view name, uint16_t bit_width, bool caller_saved) noexcept {
        vector_registers_[vector_register_count_++] = TargetRegisterInfo{id, name, RegisterBank::Vector, bit_width, caller_saved, true};
    }

    void add_instruction(OpCode opcode, uint8_t latency, uint8_t throughput, uint16_t widths, bool fusion) noexcept {
        instruction_table_[instruction_count_++] = InstructionDesc{opcode, latency, throughput, widths, fusion};
    }

    void setup_registers() noexcept;
    void setup_calling_convention() noexcept;
    void setup_instruction_table() noexcept;
};

class alignas(64) TargetMachine {
public:
    struct Capabilities {
        bool has_avx2   = false;
        bool has_avx512 = false;
        bool has_amx    = false;
        bool has_fma    = false;
        bool has_bmi2   = false;
        uint32_t cache_line_size = 64;
        uint64_t preferred_affinity_mask = 0x4ull;
        uint32_t preferred_core_index = 2;
    };

    explicit TargetMachine(ArchType arch) : m_arch(arch) {
        initialize_target();
    }

    [[nodiscard]] ArchType get_arch() const noexcept { return m_arch; }
    
    [[nodiscard]] bool supports_native(OpCode op) const noexcept {
        if (m_arch == ArchType::X86_64_AVX512 || m_arch == ArchType::X86_64_AVX2) {
            if (op == OpCode::VEC_ADD_F32_512 || op == OpCode::GATHER_F32_512)
                return m_caps.has_avx512;
            if (op == OpCode::STORE_V512 || op == OpCode::GATHER_16)
                return m_caps.has_avx2 || m_caps.has_avx512;
            return true;
        }
        return false;
    }

    [[nodiscard]] uint32_t get_gp_register_count() const noexcept {
        return static_cast<uint32_t>(description_.general_purpose_registers().size());
    }

    [[nodiscard]] const Capabilities& get_caps() const noexcept { return m_caps; }
    [[nodiscard]] const TargetDescription& description() const noexcept { return description_; }

    [[nodiscard]] static size_t get_preferred_alignment(OpCode op) noexcept {
        if (op == OpCode::STORE_V512 || op == OpCode::LOAD) return 64;
        return 8;
    }

    [[nodiscard]] bool prefer_branchless() const noexcept {
        return m_caps.has_avx512 || m_caps.has_avx2;
    }

    [[nodiscard]] bool prefers_vector_heavy_schedule() const noexcept {
        return m_caps.has_avx512 || m_caps.has_avx2 || m_arch == ArchType::AARCH64_SVE;
    }

    [[nodiscard]] float memory_latency_bias() const noexcept {
        if (m_arch == ArchType::AARCH64_SVE) return 1.05f;
        if (m_arch == ArchType::WASM_SIMD) return 1.12f;
        return 1.0f;
    }

    [[nodiscard]] uint8_t instruction_latency(OpCode op) const noexcept {
        return description_.instruction_latency(op);
    }
    [[nodiscard]] uint8_t instruction_reciprocal_throughput(OpCode op) const noexcept {
        return description_.instruction_reciprocal_throughput(op);
    }
    [[nodiscard]] float average_instruction_latency() const noexcept {
        return description_.average_instruction_latency();
    }
    [[nodiscard]] float average_instruction_reciprocal_throughput() const noexcept {
        return description_.average_instruction_reciprocal_throughput();
    }

    [[nodiscard]] const CallingConventionDesc& default_calling_convention() const noexcept {
        return description_.default_calling_convention();
    }



private:
    ArchType m_arch;
    Capabilities m_caps;
    TargetDescription description_{};

    void initialize_target();
};

// ============================================================
// Zero-Cost Policy Tags & Pipeline (Merged Logic)
// ============================================================

struct ScalarKernelPolicy {};
struct AVX2KernelPolicy   {};
struct AVX512KernelPolicy {};

template <SimdTier Tier> struct SimdPolicy;
template <> struct SimdPolicy<SimdTier::Scalar> { using type = ScalarKernelPolicy; };
template <> struct SimdPolicy<SimdTier::AVX2>   { using type = AVX2KernelPolicy;   };
template <> struct SimdPolicy<SimdTier::AVX512> { using type = AVX512KernelPolicy; };

struct TargetPipeline {
    bool enable_gvn               = true;
    bool enable_dce               = true;
    bool enable_loop_unroll       = true;
    bool run_dce_pre_inlining     = true;
    bool run_dce_post_inlining    = true;
    bool run_dce_post_specialization = true;
    bool run_dce_late_cleanup     = true;
    bool deterministic_mode       = true;
    bool safety_hardening_mode     = true;
    bool minimize_binary_size      = false;
    uint32_t optimization_level   = 3; 
};

} // namespace dgtl::core

// ============================================================
// Backward Compatibility & Global Accessors
// ============================================================

export namespace dgtl {
    using core::SimdTier;
    using core::HardwareFeatures;
    using core::detect_hardware;
}

export namespace dgtl::ir {
    using ArchType       = dgtl::core::ArchType;
    using TargetMachine  = dgtl::core::TargetMachine;
    using TargetPipeline = dgtl::core::TargetPipeline;
}
