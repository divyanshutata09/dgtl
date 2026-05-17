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

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>
#include <chrono>
#include <cstdio>
#include <iostream>

#ifdef _WIN32
#   ifndef WIN32_LEAN_AND_MEAN
#       define WIN32_LEAN_AND_MEAN
#   endif
#   include <windows.h>
#elif defined(__linux__)
#   include <sched.h>
#   include <unistd.h>
#endif

#if defined(__GNUC__) || defined(__clang__)
#   if defined(__x86_64__) || defined(__i386__)
#       include <x86intrin.h>
#   endif
#   define DGTL_HW_FORCEINLINE inline __attribute__((always_inline))
#   define DGTL_HW_AVX2_TARGET __attribute__((target("avx2")))
#elif defined(_MSC_VER)
#   include <intrin.h>
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

#include "dgtl/ir/base/IR_Node.hpp"

namespace dgtl::core {

// Bring in IR types for local scope visibility
using dgtl::ir::OpCode;

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

[[nodiscard]] inline AffinityPolicy default_high_performance_affinity() noexcept {
    return AffinityPolicy{};
}

[[nodiscard]] inline bool apply_high_performance_affinity(const AffinityPolicy& policy = default_high_performance_affinity()) noexcept {
#if defined(_WIN32)
    HANDLE process = GetCurrentProcess();
    HANDLE thread = GetCurrentThread();
    const DWORD_PTR affinity_mask = static_cast<DWORD_PTR>(policy.mask);
    const bool process_ok = SetProcessAffinityMask(process, affinity_mask) != 0;
    const bool thread_ok = SetThreadAffinityMask(thread, affinity_mask) != 0;
    const bool ideal_ok = SetThreadIdealProcessor(thread, static_cast<DWORD>(policy.preferred_core)) != static_cast<DWORD>(-1);
    bool priority_ok = true;
    if (policy.realtime_priority) {
        priority_ok = SetPriorityClass(process, REALTIME_PRIORITY_CLASS) != 0 &&
                      SetThreadPriority(thread, THREAD_PRIORITY_TIME_CRITICAL) != 0;
    }
    return process_ok && thread_ok && ideal_ok && priority_ok;
#elif defined(__linux__)
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    for (uint32_t core = 0; core < 64; ++core) {
        if ((policy.mask & (1ull << core)) != 0) {
            CPU_SET(static_cast<int>(core), &cpuset);
        }
    }
    return sched_setaffinity(0, sizeof(cpuset), &cpuset) == 0;
#else
    (void)policy;
    return false;
#endif
}

// ============================================================
// Feature Detection Logic (Unified)
// ============================================================

[[nodiscard]] inline X86SimdCaps detect_x86_simd_caps() noexcept {
    X86SimdCaps caps{};
#if DGTL_HAS_X86
    caps.has_tsc = true;
#if defined(__GNUC__) || defined(__clang__)
    caps.has_avx2 = __builtin_cpu_supports("avx2") != 0;
    caps.has_avx512 = (__builtin_cpu_supports("avx512f") != 0) &&
                      (__builtin_cpu_supports("avx512dq") != 0);
    caps.has_fma = __builtin_cpu_supports("fma") != 0;
    caps.has_bmi2 = __builtin_cpu_supports("bmi2") != 0;
#elif defined(_MSC_VER)
    int cpu_info[4] = {0, 0, 0, 0};
    __cpuidex(cpu_info, 0, 0);
    if (cpu_info[0] >= 1) {
        __cpuidex(cpu_info, 1, 0);
        caps.has_fma = (cpu_info[2] & (1 << 12)) != 0;
    }
    if (cpu_info[0] >= 7) {
        __cpuidex(cpu_info, 7, 0);
        caps.has_avx2 = (cpu_info[1] & (1 << 5)) != 0;
        caps.has_avx512 = (cpu_info[1] & (1 << 16)) != 0;
        caps.has_bmi2 = (cpu_info[1] & (1 << 8)) != 0;
    }
#endif
#endif
    return caps;
}

[[nodiscard]] inline HardwareFeatures detect_hardware() noexcept {
    static HardwareFeatures cached = []() noexcept -> HardwareFeatures {
        HardwareFeatures f;
        const X86SimdCaps caps = detect_x86_simd_caps();
        f.has_avx2 = caps.has_avx2;
        f.has_avx512 = caps.has_avx512;
        f.has_fma = caps.has_fma;
        f.has_bmi2 = caps.has_bmi2;
        f.has_tsc = caps.has_tsc;
        f.preferred_affinity_mask = default_high_performance_affinity().mask;
        f.preferred_core_index = default_high_performance_affinity().preferred_core;

        if (f.has_avx512)      f.simd = SimdTier::AVX512;
        else if (f.has_avx2)   f.simd = SimdTier::AVX2;
        else                   f.simd = SimdTier::Scalar;
        return f;
    }();
    return cached;
}

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

inline void prime_preferred_simd_units(std::size_t iterations = 2048) noexcept {
    if (preferred_simd_execution_profile() == SimdExecutionProfile::Scalar) {
        return;
    }

#if defined(__AVX2__) || defined(_MSC_VER)
    __m256 acc = _mm256_set1_ps(1.00012207f);
    const __m256 scale = _mm256_set1_ps(0.99987793f);
    const __m256 bias = _mm256_set1_ps(0.00000011f);
    for (std::size_t i = 0; i < iterations; ++i) {
        acc = _mm256_add_ps(_mm256_mul_ps(acc, scale), bias);
    }
    alignas(32) float lanes[8];
    _mm256_store_ps(lanes, acc);
    (void)lanes[0];
    _mm256_zeroupper();
#else
    volatile float acc = 1.00012207f;
    for (std::size_t i = 0; i < iterations; ++i) {
        acc = acc * 0.99987793f + 0.00000011f;
    }
    (void)acc;
#endif
}

// ============================================================
// TSC & Performance Instrumentation (Merged from Hardware.hpp)
// ============================================================

DGTL_HW_FORCEINLINE std::uint64_t read_tsc() noexcept {
#if DGTL_HAS_X86
#   if defined(_MSC_VER)
    unsigned int aux = 0;
    _mm_mfence();
    const std::uint64_t v = __rdtscp(&aux);
    _mm_lfence();
    return v;
#   else
    unsigned int aux = 0;
    __builtin_ia32_mfence();
    const std::uint64_t v = __rdtscp(&aux);
    __builtin_ia32_lfence();
    return v;
#   endif
#else
    return 0;
#endif
}

inline void stabilize() noexcept {
    using Clock = std::chrono::steady_clock;
    constexpr auto kSoakDuration = std::chrono::milliseconds{10000};
    std::clog << "[DGTL] stabilize(): 10000ms thermal soak + clock calibration\n";

    volatile std::uint64_t dummy = 0xDEADBEEFCAFEBABEull;
    const auto soak_start = Clock::now();
    while (Clock::now() - soak_start < kSoakDuration) {
        dummy = dummy * 6364136223846793005ull + 1442695040888963407ull;
    }
    if (dummy == 0) { std::clog << "[DGTL] stabilize: sink\n"; }

#if DGTL_HAS_X86
    const auto cal_start_wall = Clock::now();
    const std::uint64_t cal_start_tsc = read_tsc();
    constexpr auto kCalWindow = std::chrono::milliseconds{10};
    while (Clock::now() - cal_start_wall < kCalWindow) {
        dummy = dummy ^ (dummy >> 17);
    }
    const std::uint64_t cal_end_tsc = read_tsc();
    const auto cal_end_wall = Clock::now();

    const auto wall_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(cal_end_wall - cal_start_wall).count();
    if (wall_ns > 0 && cal_end_tsc > cal_start_tsc) {
        const double tsc_ticks = static_cast<double>(cal_end_tsc - cal_start_tsc);
        const double freq_ghz = tsc_ticks / static_cast<double>(wall_ns);
        std::clog << "[DGTL] stabilize(): TSC calibration " << static_cast<long long>(tsc_ticks) 
                  << " ticks / " << wall_ns << " ns => ~" << freq_ghz << " GHz\n";
    }
#endif
}

// ============================================================
// TargetMachine Class (Merged Architecture Awareness)
// ============================================================

struct RegisterDesc {
    uint32_t id;
    std::string_view name;
    uint32_t bit_width;
    bool is_volatile;
    bool is_vector;
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
    std::string_view name{};
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
};

struct InstructionDesc {
    OpCode opcode = OpCode::NOP;
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

    [[nodiscard]] static TargetDescription for_arch(ArchType arch) {
        TargetDescription desc;
        desc.arch_ = arch;
        desc.setup_registers();
        desc.setup_calling_convention();
        desc.setup_instruction_table();
        return desc;
    }

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
        for (size_t i = 0; i < instruction_count_; ++i) {
            if (instruction_table_[i].opcode == opcode) {
                return instruction_table_[i];
            }
        }
        return std::nullopt;
    }
    [[nodiscard]] uint8_t instruction_latency(OpCode opcode) const noexcept {
        const auto desc = instruction_desc(opcode);
        return desc.has_value() ? desc->latency_cycles : 1;
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

    void setup_registers() noexcept {
        gp_register_count_ = 0;
        vector_register_count_ = 0;
        supported_scalar_widths_ = kWidth32 | kWidth64;
        supported_vector_widths_ = 0;

        switch (arch_) {
            case ArchType::X86_64_AVX512:
            case ArchType::X86_64_AVX2:
                add_gp(0, "rax", true);
                add_gp(1, "rcx", true);
                add_gp(2, "rdx", true);
                add_gp(3, "rbx", false);
                add_gp(4, "rsp", false);
                add_gp(5, "rbp", false);
                add_gp(6, "rsi", false);
                add_gp(7, "rdi", false);
                add_gp(8, "r8", true);
                add_gp(9, "r9", true);
                add_gp(10, "r10", true);
                add_gp(11, "r11", true);
                add_gp(12, "r12", false);
                add_gp(13, "r13", false);
                add_gp(14, "r14", false);
                add_gp(15, "r15", false);
                for (uint16_t i = 0; i < 16; ++i) {
                    add_vec(i, i < 10 ? std::string_view{"xmm"}.substr(0, 3) : std::string_view{"xmm"}.substr(0, 3), arch_ == ArchType::X86_64_AVX512 ? 512 : 256, true);
                    vector_registers_[i].name = (arch_ == ArchType::X86_64_AVX512)
                        ? std::array<std::string_view, 16>{"zmm0","zmm1","zmm2","zmm3","zmm4","zmm5","zmm6","zmm7","zmm8","zmm9","zmm10","zmm11","zmm12","zmm13","zmm14","zmm15"}[i]
                        : std::array<std::string_view, 16>{"ymm0","ymm1","ymm2","ymm3","ymm4","ymm5","ymm6","ymm7","ymm8","ymm9","ymm10","ymm11","ymm12","ymm13","ymm14","ymm15"}[i];
                }
                supported_vector_widths_ = (arch_ == ArchType::X86_64_AVX512) ? (kWidth128 | kWidth256 | kWidth512) : (kWidth128 | kWidth256);
                break;
            case ArchType::AARCH64_SVE:
                for (uint16_t i = 0; i < 31; ++i) {
                    add_gp(i, i == 29 ? "fp" : (i == 30 ? "lr" : "x"), i < 19);
                    gp_registers_[i].name = (i == 29) ? "fp" : ((i == 30) ? "lr" : std::array<std::string_view, 31>{"x0","x1","x2","x3","x4","x5","x6","x7","x8","x9","x10","x11","x12","x13","x14","x15","x16","x17","x18","x19","x20","x21","x22","x23","x24","x25","x26","x27","x28","fp","lr"}[i]);
                }
                for (uint16_t i = 0; i < 32; ++i) {
                    add_vec(i, std::array<std::string_view, 32>{"v0","v1","v2","v3","v4","v5","v6","v7","v8","v9","v10","v11","v12","v13","v14","v15","v16","v17","v18","v19","v20","v21","v22","v23","v24","v25","v26","v27","v28","v29","v30","v31"}[i], 128, i < 8);
                }
                supported_vector_widths_ = kWidth128;
                break;
            case ArchType::WASM_SIMD:
                supported_scalar_widths_ = kWidth32 | kWidth64;
                supported_vector_widths_ = kWidth128;
                break;
            case ArchType::FPGA_VERILOG:
            default:
                supported_scalar_widths_ = kWidth32 | kWidth64 | kWidth128;
                break;
        }
    }

    void setup_calling_convention() noexcept {
        switch (arch_) {
            case ArchType::X86_64_AVX512:
            case ArchType::X86_64_AVX2:
                calling_convention_.kind = CallingConventionKind::SystemV_x64;
                calling_convention_.name = "sysv-x64";
                calling_convention_.integer_arg_registers = {5, 4, 1, 2, 8, 9, 0, 0};
                calling_convention_.vector_arg_registers = {0, 1, 2, 3, 4, 5, 6, 7};
                calling_convention_.integer_arg_count = 6;
                calling_convention_.vector_arg_count = 8;
                calling_convention_.integer_return_register = 0;
                calling_convention_.vector_return_register = 0;
                calling_convention_.stack_alignment = 16;
                calling_convention_.shadow_space_bytes = 0;
                break;
            case ArchType::AARCH64_SVE:
                calling_convention_.kind = CallingConventionKind::AAPCS64;
                calling_convention_.name = "aapcs64";
                calling_convention_.integer_arg_registers = {0, 1, 2, 3, 4, 5, 6, 7};
                calling_convention_.vector_arg_registers = {0, 1, 2, 3, 4, 5, 6, 7};
                calling_convention_.integer_arg_count = 8;
                calling_convention_.vector_arg_count = 8;
                calling_convention_.integer_return_register = 0;
                calling_convention_.vector_return_register = 0;
                calling_convention_.stack_alignment = 16;
                calling_convention_.shadow_space_bytes = 0;
                break;
            case ArchType::WASM_SIMD:
                calling_convention_.kind = CallingConventionKind::WasmCABI;
                calling_convention_.name = "wasm-cabi";
                calling_convention_.stack_alignment = 16;
                break;
            case ArchType::FPGA_VERILOG:
            default:
                calling_convention_.kind = CallingConventionKind::BareMetal;
                calling_convention_.name = "bare-metal";
                calling_convention_.stack_alignment = 16;
                break;
        }
    }

    void setup_instruction_table() noexcept {
        instruction_count_ = 0;
        supports_fma_ = false;
        switch (arch_) {
            case ArchType::X86_64_AVX512:
            case ArchType::X86_64_AVX2:
                supports_fma_ = true;
                add_instruction(OpCode::ADD_I32, 1, 1, kWidth32, true);
                add_instruction(OpCode::ADD_I64, 1, 1, kWidth64, true);
                add_instruction(OpCode::MUL_I32, 3, 1, kWidth32, false);
                add_instruction(OpCode::MUL_I64, 3, 1, kWidth64, false);
                add_instruction(OpCode::ADD_F32, 3, 1, kWidth32, true);
                add_instruction(OpCode::MUL_F32, 4, 1, kWidth32, true);
                add_instruction(OpCode::FMA_F32, 4, 1, kWidth32, true);
                add_instruction(OpCode::VEC_ADD_F32_512, 4, 1, kWidth512, true);
                add_instruction(OpCode::VEC_MUL_F32_512, 4, 1, kWidth512, true);
                break;
            case ArchType::AARCH64_SVE:
                add_instruction(OpCode::ADD_I64, 1, 1, kWidth64, true);
                add_instruction(OpCode::ADD_F32, 4, 1, kWidth32, true);
                add_instruction(OpCode::MUL_F32, 4, 1, kWidth32, false);
                break;
            case ArchType::WASM_SIMD:
                add_instruction(OpCode::ADD_I32, 1, 1, kWidth32, false);
                add_instruction(OpCode::ADD_I64, 1, 1, kWidth64, false);
                break;
            case ArchType::FPGA_VERILOG:
            default:
                add_instruction(OpCode::ADD_I32, 1, 1, kWidth32 | kWidth64 | kWidth128, false);
                add_instruction(OpCode::ADD_I64, 1, 1, kWidth64 | kWidth128, false);
                break;
        }
    }
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

    [[nodiscard]] size_t get_preferred_alignment(OpCode op) const noexcept {
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

    [[nodiscard]] const CallingConventionDesc& default_calling_convention() const noexcept {
        return description_.default_calling_convention();
    }

    [[nodiscard]] AffinityPolicy default_affinity_policy() const noexcept {
        return AffinityPolicy{m_caps.preferred_affinity_mask, m_caps.preferred_core_index, true};
    }

    [[nodiscard]] bool apply_default_affinity() const noexcept {
        return apply_high_performance_affinity(default_affinity_policy());
    }

private:
    ArchType m_arch;
    Capabilities m_caps;
    TargetDescription description_{};

    void initialize_target() {
        description_ = TargetDescription::for_arch(m_arch);
        if (m_arch == ArchType::X86_64_AVX512 || m_arch == ArchType::X86_64_AVX2) {
            const X86SimdCaps host_caps = detect_x86_simd_caps();
            m_caps.has_avx2   = host_caps.has_avx2;
            m_caps.has_avx512 = host_caps.has_avx512;
            m_caps.has_fma    = host_caps.has_fma;
            m_caps.has_bmi2   = host_caps.has_bmi2;
            m_caps.cache_line_size = 64;
            m_caps.preferred_affinity_mask = default_high_performance_affinity().mask;
            m_caps.preferred_core_index = default_high_performance_affinity().preferred_core;
        }
    }
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

namespace dgtl {
    using core::SimdTier;
    using core::HardwareFeatures;
    using core::detect_hardware;
    using core::stabilize;
    using core::read_tsc;
}

namespace dgtl::ir {
    using ArchType       = dgtl::core::ArchType;
    using TargetMachine  = dgtl::core::TargetMachine;
    using TargetPipeline = dgtl::core::TargetPipeline;
}