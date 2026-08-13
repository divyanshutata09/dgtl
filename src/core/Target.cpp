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
module;

#include <array>
#include <cstdint>
#include <cstddef>
#include <string_view>
#include <optional>
#include <chrono>

#if defined(__linux__)
#   include <sched.h>
#   include <unistd.h>
#endif

#ifdef _WIN32
#   ifndef WIN32_LEAN_AND_MEAN
#       define WIN32_LEAN_AND_MEAN
#   endif
#   include <windows.h>
#elif defined(__linux__)
#   include <sched.h>
#   include <unistd.h>
#endif

#if defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64)
#   if defined(_MSC_VER)
#       include <intrin.h>
#   else
#       include <x86intrin.h>
#   endif
#endif

module dgtl.target;

namespace dgtl::core {

[[nodiscard]] AffinityPolicy default_high_performance_affinity() noexcept {
    return AffinityPolicy{
        .mask = 0x4ull,
        .preferred_core = 2,
        .realtime_priority = true
    };
}

[[nodiscard]] bool apply_high_performance_affinity(const AffinityPolicy& policy) noexcept {
#ifdef _WIN32
    HANDLE thread = GetCurrentThread();
    DWORD_PTR mask = static_cast<DWORD_PTR>(policy.mask);
    if (SetThreadAffinityMask(thread, mask) == 0) {
        return false;
    }
    if (policy.realtime_priority) {
        SetThreadPriority(thread, THREAD_PRIORITY_TIME_CRITICAL);
    }
    return true;
#elif defined(__linux__)
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    for (int i = 0; i < 64; ++i) {
        if ((policy.mask & (1ull << i)) != 0) {
            CPU_SET(i, &cpuset);
        }
    }
    if (sched_setaffinity(0, sizeof(cpu_set_t), &cpuset) != 0) {
        return false;
    }
    if (policy.realtime_priority) {
        struct sched_param param;
        param.sched_priority = sched_get_priority_max(SCHED_FIFO);
        sched_setscheduler(0, SCHED_FIFO, &param);
    }
    return true;
#else
    (void)policy;
    return false;
#endif
}

// ---------------------------------------------------------------------------
// GCC 15 ICE workaround (cp/module.cc:9996)
// These functions use __builtin_cpu_supports(), __rdtsc(), or AVX intrinsics.
// GCC cannot serialize those compiler-built-in tree nodes into the BMI when
// the function is declared `inline` in the module interface; they must be
// defined out-of-line in this module implementation unit instead.
// ---------------------------------------------------------------------------

[[nodiscard]] std::uint64_t read_tsc() noexcept {
#if defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64)
    return __rdtsc();
#else
    using clock = std::chrono::steady_clock;
    const auto now = clock::now().time_since_epoch();
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
#endif
}

void stabilize() noexcept {
    constexpr std::size_t kFixedRounds = 1u << 21;
    std::uint64_t spin_sink = 0x9E3779B97F4A7C15ull;
    for (std::size_t i = 0; i < kFixedRounds; ++i) {
        spin_sink ^= (spin_sink << 7);
        spin_sink ^= (spin_sink >> 9);
        spin_sink += 0xBF58476D1CE4E5B9ull + static_cast<std::uint64_t>(i);
    }
    (void)spin_sink;
}

[[nodiscard]] X86SimdCaps detect_x86_simd_caps() noexcept {
    X86SimdCaps caps{};
#if defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64)
    caps.has_tsc = true;
#   if defined(__GNUC__) || defined(__clang__)
    caps.has_avx2   = __builtin_cpu_supports("avx2")    != 0;
    caps.has_avx512 = (__builtin_cpu_supports("avx512f")  != 0) &&
                      (__builtin_cpu_supports("avx512dq") != 0);
    caps.has_fma    = __builtin_cpu_supports("fma")     != 0;
    caps.has_bmi2   = __builtin_cpu_supports("bmi2")    != 0;
#   elif defined(_MSC_VER)
    int cpu_info[4] = {0, 0, 0, 0};
    __cpuidex(cpu_info, 0, 0);
    if (cpu_info[0] >= 1) {
        __cpuidex(cpu_info, 1, 0);
        caps.has_fma = (cpu_info[2] & (1 << 12)) != 0;
    }
    if (cpu_info[0] >= 7) {
        __cpuidex(cpu_info, 7, 0);
        caps.has_avx2   = (cpu_info[1] & (1 << 5))  != 0;
        caps.has_avx512 = (cpu_info[1] & (1 << 16)) != 0;
        caps.has_bmi2   = (cpu_info[1] & (1 << 8))  != 0;
    }
#   endif
#endif
    return caps;
}

[[nodiscard]] HardwareFeatures detect_hardware_impl() noexcept {
    HardwareFeatures f;
    const X86SimdCaps caps = detect_x86_simd_caps();
    f.has_avx2   = caps.has_avx2;
    f.has_avx512 = caps.has_avx512;
    f.has_fma    = caps.has_fma;
    f.has_bmi2   = caps.has_bmi2;
    f.has_tsc    = caps.has_tsc;
    f.preferred_affinity_mask = default_high_performance_affinity().mask;
    f.preferred_core_index    = default_high_performance_affinity().preferred_core;
    if (f.has_avx512)    f.simd = SimdTier::AVX512;
    else if (f.has_avx2) f.simd = SimdTier::AVX2;
    else                 f.simd = SimdTier::Scalar;
    return f;
}

void prime_preferred_simd_units(std::size_t iterations) noexcept {
    if (preferred_simd_execution_profile() == SimdExecutionProfile::Scalar) {
        return;
    }
#if defined(__AVX2__) || defined(_MSC_VER)
    __m256 acc         = _mm256_set1_ps(1.00012207f);
    const __m256 scale = _mm256_set1_ps(0.99987793f);
    const __m256 bias  = _mm256_set1_ps(0.00000011f);
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

[[nodiscard]] TargetDescription TargetDescription::for_arch(ArchType arch) {

    TargetDescription desc;
    desc.arch_ = arch;
    desc.setup_registers();
    desc.setup_calling_convention();
    desc.setup_instruction_table();
    return desc;
}

void TargetDescription::setup_registers() noexcept {
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
                add_vec(i, "ymm", arch_ == ArchType::X86_64_AVX512 ? 512 : 256, true);
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

void TargetDescription::setup_calling_convention() noexcept {
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

void TargetDescription::setup_instruction_table() noexcept {
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

void TargetMachine::initialize_target() {
    description_ = TargetDescription::for_arch(m_arch);
    if (m_arch == ArchType::X86_64_AVX512 || m_arch == ArchType::X86_64_AVX2) {
        const X86SimdCaps host_caps = detect_x86_simd_caps();
        m_caps.has_avx2   = host_caps.has_avx2;
        m_caps.has_avx512 = host_caps.has_avx512;
        m_caps.has_fma    = host_caps.has_fma;
        m_caps.has_bmi2   = host_caps.has_bmi2;
        m_caps.cache_line_size = 64;
    }
}


} // namespace dgtl::core
