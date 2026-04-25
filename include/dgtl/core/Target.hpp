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

#include <cstddef>
#include <cstdint>
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

#include "dgtl/ir/Global_IR.hpp"

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
    constexpr auto kSoakDuration = std::chrono::milliseconds{3000};
    std::clog << "[DGTL] stabilize(): 3000ms thermal soak + clock calibration\n";

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
        return (m_arch == ArchType::X86_64_AVX512 || m_arch == ArchType::X86_64_AVX2) ? 16 : 32;
    }

    [[nodiscard]] const Capabilities& get_caps() const noexcept { return m_caps; }

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

    [[nodiscard]] AffinityPolicy default_affinity_policy() const noexcept {
        return AffinityPolicy{m_caps.preferred_affinity_mask, m_caps.preferred_core_index, true};
    }

    [[nodiscard]] bool apply_default_affinity() const noexcept {
        return apply_high_performance_affinity(default_affinity_policy());
    }

private:
    ArchType m_arch;
    Capabilities m_caps;

    void initialize_target() {
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