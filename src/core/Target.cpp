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
#include <algorithm>
#include <cstdint>
#include <cstddef>
#include <string_view>
#include <optional>
#include <chrono>

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
        .mask = 0x1ull,
        .preferred_core = 0,
        .realtime_priority = false
    };
}

[[nodiscard]] bool apply_high_performance_affinity(const AffinityPolicy& policy) noexcept {
#ifdef _WIN32
    HANDLE thread = GetCurrentThread();
    const DWORD_PTR mask = static_cast<DWORD_PTR>(policy.mask);
    if (mask == 0 || SetThreadAffinityMask(thread, mask) == 0) {
        return false;
    }
    if (policy.realtime_priority &&
        SetThreadPriority(thread, THREAD_PRIORITY_TIME_CRITICAL) == 0) {
        return false;
    }
    return true;
#elif defined(__linux__)
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    bool any_cpu = false;
    for (int i = 0; i < CPU_SETSIZE && i < 64; ++i) {
        if ((policy.mask & (1ull << i)) != 0) {
            CPU_SET(i, &cpuset);
            any_cpu = true;
        }
    }
    if (!any_cpu || sched_setaffinity(0, sizeof(cpuset), &cpuset) != 0) {
        return false;
    }
    if (policy.realtime_priority) {
        const int max_priority = sched_get_priority_max(SCHED_FIFO);
        if (max_priority < 0) {
            return false;
        }
        sched_param param{};
        param.sched_priority = max_priority;
        if (sched_setscheduler(0, SCHED_FIFO, &param) != 0) {
            return false;
        }
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
    const int max_basic_leaf = cpu_info[0];

    bool os_avx_state = false;
    bool os_avx512_state = false;
    if (max_basic_leaf >= 1) {
        __cpuidex(cpu_info, 1, 0);
        const bool hardware_avx = (cpu_info[2] & (1 << 28)) != 0;
        const bool osxsave = (cpu_info[2] & (1 << 27)) != 0;
        if (hardware_avx && osxsave) {
            const unsigned __int64 xcr0 = _xgetbv(0);
            os_avx_state = (xcr0 & 0x6u) == 0x6u;
            os_avx512_state = (xcr0 & 0xE6u) == 0xE6u;
        }
        caps.has_fma = os_avx_state && ((cpu_info[2] & (1 << 12)) != 0);
    }
    if (max_basic_leaf >= 7) {
        __cpuidex(cpu_info, 7, 0);
        caps.has_avx2   = os_avx_state && ((cpu_info[1] & (1 << 5)) != 0);
        const bool avx512f = (cpu_info[1] & (1 << 16)) != 0;
        const bool avx512dq = (cpu_info[1] & (1 << 17)) != 0;
        caps.has_avx512 = os_avx512_state && avx512f && avx512dq;
        caps.has_bmi2   = (cpu_info[1] & (1 << 8)) != 0;
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
#if defined(__AVX2__)
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

[[nodiscard]] TargetDescription TargetDescription::for_arch(ArchType arch, CallingConventionKind calling_convention) {

    TargetDescription desc;
    desc.arch_ = arch;
    desc.setup_registers();
    desc.setup_calling_convention(calling_convention);
    desc.setup_instruction_table();
    return desc;
}

void TargetDescription::setup_registers() noexcept {
    gp_register_count_ = 0;
    vector_register_count_ = 0;
    supported_scalar_widths_ = kWidth32 | kWidth64;
    supported_vector_widths_ = 0;
    scalable_vectors_ = false;

    switch (arch_) {
        case ArchType::X86_64_AVX512:
        case ArchType::X86_64_AVX2: {
            static constexpr std::array<std::string_view, 15> gp_names{
                "rcx","rdx","rbx","rsp","rbp","rsi","rdi","r8",
                "r9","r10","r11","r12","r13","r14","r15"};
            static constexpr std::array<bool, 15> caller_saved{
                true,true,false,false,false,true,true,true,true,true,true,false,false,false,false};

            for (uint16_t i = 0; i < gp_names.size(); ++i) {
                add_gp(static_cast<uint16_t>(i + 1), gp_names[i], caller_saved[i]);
            }

            const uint16_t vector_count = (arch_ == ArchType::X86_64_AVX512) ? 32 : 16;
            for (uint16_t i = 0; i < vector_count; ++i) {
                if (arch_ == ArchType::X86_64_AVX512) {
                    static constexpr std::array<std::string_view, 32> names{
                        "zmm0","zmm1","zmm2","zmm3","zmm4","zmm5","zmm6","zmm7",
                        "zmm8","zmm9","zmm10","zmm11","zmm12","zmm13","zmm14","zmm15",
                        "zmm16","zmm17","zmm18","zmm19","zmm20","zmm21","zmm22","zmm23",
                        "zmm24","zmm25","zmm26","zmm27","zmm28","zmm29","zmm30","zmm31"};
                    add_vec(i, names[i], 512, true);
                } else {
                    static constexpr std::array<std::string_view, 16> names{
                        "ymm0","ymm1","ymm2","ymm3","ymm4","ymm5","ymm6","ymm7",
                        "ymm8","ymm9","ymm10","ymm11","ymm12","ymm13","ymm14","ymm15"};
                    add_vec(i, names[i], 256, true);
                }
            }

            supported_vector_widths_ = (arch_ == ArchType::X86_64_AVX512)
                ? (kWidth128 | kWidth256 | kWidth512)
                : (kWidth128 | kWidth256);
            break;
        }

        case ArchType::AARCH64_SVE: {
            static constexpr std::array<std::string_view, 31> gp_names{
                "x0","x1","x2","x3","x4","x5","x6","x7","x8","x9","x10","x11","x12","x13","x14","x15",
                "x16","x17","x18","x19","x20","x21","x22","x23","x24","x25","x26","x27","x28","fp","lr"};
            for (uint16_t i = 0; i < gp_names.size(); ++i) {
                add_gp(i, gp_names[i], i < 19);
            }

            static constexpr std::array<std::string_view, 32> vec_names{
                "z0","z1","z2","z3","z4","z5","z6","z7","z8","z9","z10","z11","z12","z13","z14","z15",
                "z16","z17","z18","z19","z20","z21","z22","z23","z24","z25","z26","z27","z28","z29","z30","z31"};
            for (uint16_t i = 0; i < vec_names.size(); ++i) {
                add_vec(i, vec_names[i], 128, i < 8);
            }
            supported_vector_widths_ = kWidth128;
            scalable_vectors_ = true;
            break;
        }

        case ArchType::ARM32_NEON: {
            static constexpr std::array<std::string_view, 16> gp_names{
                "r0","r1","r2","r3","r4","r5","r6","r7",
                "r8","r9","r10","r11","r12","sp","lr","pc"};
            for (uint16_t i = 0; i < gp_names.size(); ++i) {
                add_gp(i, gp_names[i], i < 4 || i == 12);
            }
            static constexpr std::array<std::string_view, 16> vec_names{
                "q0","q1","q2","q3","q4","q5","q6","q7",
                "q8","q9","q10","q11","q12","q13","q14","q15"};
            for (uint16_t i = 0; i < vec_names.size(); ++i) {
                add_vec(i, vec_names[i], 128, true);
            }
            supported_scalar_widths_ = kWidth32 | kWidth64;
            supported_vector_widths_ = kWidth64 | kWidth128;
            break;
        }

        case ArchType::RISCV64_RVV: {
            static constexpr std::array<std::string_view, 32> gp_names{
                "x0","ra","sp","gp","tp","t0","t1","t2",
                "s0","s1","a0","a1","a2","a3","a4","a5",
                "a6","a7","s2","s3","s4","s5","s6","s7",
                "s8","s9","s10","s11","t3","t4","t5","t6"};
            for (uint16_t i = 0; i < gp_names.size(); ++i) {
                const bool caller_saved = i == 1 || (i >= 5 && i <= 7) ||
                                          (i >= 10 && i <= 17) || (i >= 28);
                add_gp(i, gp_names[i], caller_saved);
            }

            static constexpr std::array<std::string_view, 32> vec_names{
                "v0","v1","v2","v3","v4","v5","v6","v7",
                "v8","v9","v10","v11","v12","v13","v14","v15",
                "v16","v17","v18","v19","v20","v21","v22","v23",
                "v24","v25","v26","v27","v28","v29","v30","v31"};
            for (uint16_t i = 0; i < vec_names.size(); ++i) {
                add_vec(i, vec_names[i], 128, i < 8 || i >= 24);
            }
            supported_vector_widths_ = kWidth128 | kWidth256 | kWidth512;
            scalable_vectors_ = true;
            break;
        }

        case ArchType::WASM_SIMD:
            supported_vector_widths_ = kWidth128;
            break;

        case ArchType::CUDA_PTX: {
            static constexpr std::array<std::string_view, 32> reg_names{
                "r0","r1","r2","r3","r4","r5","r6","r7",
                "r8","r9","r10","r11","r12","r13","r14","r15",
                "r16","r17","r18","r19","r20","r21","r22","r23",
                "r24","r25","r26","r27","r28","r29","r30","r31"};
            for (uint16_t i = 0; i < reg_names.size(); ++i) {
                add_gp(i, reg_names[i], true);
            }
            static constexpr std::array<std::string_view, 32> vec_names{
                "v0","v1","v2","v3","v4","v5","v6","v7",
                "v8","v9","v10","v11","v12","v13","v14","v15",
                "v16","v17","v18","v19","v20","v21","v22","v23",
                "v24","v25","v26","v27","v28","v29","v30","v31"};
            for (uint16_t i = 0; i < vec_names.size(); ++i) {
                add_vec(i, vec_names[i], 32, true);
            }
            supported_vector_widths_ = kWidth32 | kWidth64 | kWidth128 | kWidth256;
            break;
        }

        case ArchType::ROCM_AMDGCN: {
            static constexpr std::array<std::string_view, 32> scalar_names{
                "s0","s1","s2","s3","s4","s5","s6","s7",
                "s8","s9","s10","s11","s12","s13","s14","s15",
                "s16","s17","s18","s19","s20","s21","s22","s23",
                "s24","s25","s26","s27","s28","s29","s30","s31"};
            for (uint16_t i = 0; i < scalar_names.size(); ++i) {
                add_gp(i, scalar_names[i], true);
            }
            static constexpr std::array<std::string_view, 32> vector_names{
                "v0","v1","v2","v3","v4","v5","v6","v7",
                "v8","v9","v10","v11","v12","v13","v14","v15",
                "v16","v17","v18","v19","v20","v21","v22","v23",
                "v24","v25","v26","v27","v28","v29","v30","v31"};
            for (uint16_t i = 0; i < vector_names.size(); ++i) {
                add_vec(i, vector_names[i], 32, true);
            }
            supported_vector_widths_ = kWidth32 | kWidth64 | kWidth128 | kWidth256;
            break;
        }

        case ArchType::FPGA_VERILOG:
            supported_scalar_widths_ = kWidth32 | kWidth64 | kWidth128;
            break;
    }
}

void TargetDescription::setup_calling_convention(CallingConventionKind calling_convention) noexcept {
    calling_convention_ = CallingConventionDesc{};

    switch (arch_) {
        case ArchType::X86_64_AVX512:
        case ArchType::X86_64_AVX2:
            if (calling_convention == CallingConventionKind::Windows_x64) {
                calling_convention_.kind = CallingConventionKind::Windows_x64;
                calling_convention_.name = "windows-x64";
                calling_convention_.integer_arg_registers = {1, 2, 8, 9, 0, 0, 0, 0};
                calling_convention_.vector_arg_registers = {0, 1, 2, 3};
                calling_convention_.integer_arg_count = 4;
                calling_convention_.vector_arg_count = 4;
                calling_convention_.integer_return_register = 0;
                calling_convention_.vector_return_register = 0;
                calling_convention_.stack_alignment = 16;
                calling_convention_.shadow_space_bytes = 32;
            } else {
                calling_convention_.kind = CallingConventionKind::SystemV_x64;
                calling_convention_.name = "sysv-x64";
                calling_convention_.integer_arg_registers = {7, 6, 2, 1, 8, 9, 0, 0};
                calling_convention_.vector_arg_registers = {0, 1, 2, 3, 4, 5, 6, 7};
                calling_convention_.integer_arg_count = 6;
                calling_convention_.vector_arg_count = 8;
                calling_convention_.integer_return_register = 0;
                calling_convention_.vector_return_register = 0;
                calling_convention_.stack_alignment = 16;
                calling_convention_.shadow_space_bytes = 0;
            }
            break;

        case ArchType::AARCH64_SVE:
            calling_convention_.kind = CallingConventionKind::AAPCS64;
            calling_convention_.name = "aapcs64";
            calling_convention_.integer_arg_registers = {0,1,2,3,4,5,6,7};
            calling_convention_.vector_arg_registers = {0,1,2,3,4,5,6,7};
            calling_convention_.integer_arg_count = 8;
            calling_convention_.vector_arg_count = 8;
            calling_convention_.integer_return_register = 0;
            calling_convention_.vector_return_register = 0;
            calling_convention_.stack_alignment = 16;
            break;

        case ArchType::ARM32_NEON:
            calling_convention_.kind = CallingConventionKind::AAPCS32;
            calling_convention_.name = "aapcs32";
            calling_convention_.integer_arg_registers = {0,1,2,3,0,0,0,0};
            calling_convention_.integer_arg_count = 4;
            calling_convention_.vector_arg_registers = {0,1,2,3,4,5,6,7};
            calling_convention_.vector_arg_count = 8;
            calling_convention_.integer_return_register = 0;
            calling_convention_.vector_return_register = 0;
            calling_convention_.stack_alignment = 8;
            break;

        case ArchType::RISCV64_RVV:
            calling_convention_.kind = CallingConventionKind::RiscV64;
            calling_convention_.name = "riscv64";
            calling_convention_.integer_arg_registers = {10,11,12,13,14,15,16,17};
            calling_convention_.integer_arg_count = 8;
            calling_convention_.vector_arg_registers = {0,1,2,3,4,5,6,7};
            calling_convention_.vector_arg_count = 8;
            calling_convention_.integer_return_register = 10;
            calling_convention_.vector_return_register = 0;
            calling_convention_.stack_alignment = 16;
            break;

        case ArchType::WASM_SIMD:
            calling_convention_.kind = CallingConventionKind::WasmCABI;
            calling_convention_.name = "wasm-cabi";
            calling_convention_.stack_alignment = 16;
            break;

        case ArchType::CUDA_PTX:
            calling_convention_.kind = CallingConventionKind::CUDADevice;
            calling_convention_.name = "cuda-device";
            calling_convention_.stack_alignment = 16;
            break;

        case ArchType::ROCM_AMDGCN:
            calling_convention_.kind = CallingConventionKind::ROCmDevice;
            calling_convention_.name = "rocm-device";
            calling_convention_.stack_alignment = 16;
            break;

        case ArchType::FPGA_VERILOG:
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

        case ArchType::X86_64_AVX2:
            supports_fma_ = true;
            add_instruction(OpCode::ADD_I32, 1, 1, kWidth32, true);
            add_instruction(OpCode::ADD_I64, 1, 1, kWidth64, true);
            add_instruction(OpCode::MUL_I32, 3, 1, kWidth32, false);
            add_instruction(OpCode::MUL_I64, 3, 1, kWidth64, false);
            add_instruction(OpCode::ADD_F32, 3, 1, kWidth32, true);
            add_instruction(OpCode::MUL_F32, 4, 1, kWidth32, true);
            add_instruction(OpCode::FMA_F32, 4, 1, kWidth32, true);
            break;

        case ArchType::AARCH64_SVE:
            supports_fma_ = true;
            add_instruction(OpCode::ADD_I64, 1, 1, kWidth64, true);
            add_instruction(OpCode::ADD_F32, 4, 1, kWidth32, true);
            add_instruction(OpCode::MUL_F32, 4, 1, kWidth32, false);
            add_instruction(OpCode::FMA_F32, 4, 1, kWidth32, true);
            break;

        case ArchType::ARM32_NEON:
            supports_fma_ = true;
            add_instruction(OpCode::ADD_I32, 1, 1, kWidth32, true);
            add_instruction(OpCode::MUL_I32, 3, 1, kWidth32, false);
            add_instruction(OpCode::ADD_F32, 4, 1, kWidth32, true);
            add_instruction(OpCode::MUL_F32, 4, 1, kWidth32, false);
            add_instruction(OpCode::FMA_F32, 4, 1, kWidth32, true);
            break;

        case ArchType::RISCV64_RVV:
            supports_fma_ = true;
            add_instruction(OpCode::ADD_I32, 1, 1, kWidth32, true);
            add_instruction(OpCode::ADD_I64, 1, 1, kWidth64, true);
            add_instruction(OpCode::MUL_I32, 3, 1, kWidth32, false);
            add_instruction(OpCode::MUL_I64, 3, 1, kWidth64, false);
            add_instruction(OpCode::ADD_F32, 4, 1, kWidth32, true);
            add_instruction(OpCode::MUL_F32, 4, 1, kWidth32, false);
            add_instruction(OpCode::FMA_F32, 4, 1, kWidth32, true);
            break;

        case ArchType::WASM_SIMD:
            add_instruction(OpCode::ADD_I32, 1, 1, kWidth32, false);
            add_instruction(OpCode::ADD_I64, 1, 1, kWidth64, false);
            break;

        case ArchType::CUDA_PTX:
        case ArchType::ROCM_AMDGCN:
            supports_fma_ = true;
            add_instruction(OpCode::ADD_I32, 1, 1, kWidth32, false);
            add_instruction(OpCode::ADD_I64, 1, 1, kWidth64, false);
            add_instruction(OpCode::MUL_I32, 4, 1, kWidth32, false);
            add_instruction(OpCode::MUL_I64, 4, 1, kWidth64, false);
            add_instruction(OpCode::ADD_F32, 4, 1, kWidth32, false);
            add_instruction(OpCode::MUL_F32, 4, 1, kWidth32, false);
            add_instruction(OpCode::FMA_F32, 4, 1, kWidth32, false);
            break;

        case ArchType::FPGA_VERILOG:
            add_instruction(OpCode::ADD_I32, 1, 1, kWidth32 | kWidth64 | kWidth128, false);
            add_instruction(OpCode::ADD_I64, 1, 1, kWidth64 | kWidth128, false);
            break;
    }
}

std::optional<ArchType> TargetMachine::arch_from_target_name(std::string_view name) noexcept {
    auto eq = [](std::string_view a, std::string_view b) noexcept {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i) {
            const auto lower = [](char ch) noexcept {
                return (ch >= 'A' && ch <= 'Z') ? static_cast<char>(ch - 'A' + 'a') : ch;
            };
            if (lower(a[i]) != lower(b[i])) return false;
        }
        return true;
    };

    if (eq(name, "x64") || eq(name, "x86_64") || eq(name, "amd64") ||
        eq(name, "x86-64") || eq(name, "x86_64_avx2")) {
        return ArchType::X86_64_AVX2;
    }
    if (eq(name, "x86_64_avx512")) return ArchType::X86_64_AVX512;
    if (eq(name, "arm64") || eq(name, "aarch64") || eq(name, "armv8") || eq(name, "sve") || eq(name, "arm64_sve")) {
        return ArchType::AARCH64_SVE;
    }
    if (eq(name, "arm32") || eq(name, "armv7a") || eq(name, "armv7") || eq(name, "arm32_neon")) {
        return ArchType::ARM32_NEON;
    }
    if (eq(name, "riscv64") || eq(name, "rv64") || eq(name, "rv64gc") || eq(name, "riscv64_rvv")) {
        return ArchType::RISCV64_RVV;
    }
    if (eq(name, "wasm") || eq(name, "wasm32") || eq(name, "wasm64") || eq(name, "wasm_simd")) {
        return ArchType::WASM_SIMD;
    }
    if (eq(name, "cuda") || eq(name, "ptx") || eq(name, "nvptx") || eq(name, "cuda_ptx")) {
        return ArchType::CUDA_PTX;
    }
    if (eq(name, "rocm") || eq(name, "amdgcn") || eq(name, "amd-gcn") ||
        eq(name, "amdgcn-gfx") || eq(name, "hip") || eq(name, "rocm_amdgcn")) {
        return ArchType::ROCM_AMDGCN;
    }
    return std::nullopt;
}

std::optional<ObjectFormat> TargetMachine::object_format_from_target_name(
    std::string_view name) noexcept {
    auto eq = [](std::string_view a, std::string_view b) noexcept {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i) {
            const auto lower = [](char ch) noexcept {
                return (ch >= 'A' && ch <= 'Z') ? static_cast<char>(ch - 'A' + 'a') : ch;
            };
            if (lower(a[i]) != lower(b[i])) return false;
        }
        return true;
    };
    if (eq(name, "macho") || eq(name, "mach-o") || eq(name, "darwin")) {
        return ObjectFormat::MachO;
    }
    if (eq(name, "wasm") || eq(name, "wasm32") || eq(name, "wasm64") || eq(name, "wasm_simd")) {
        return ObjectFormat::Wasm;
    }
    return std::nullopt;
}

void TargetMachine::initialize_target() {
    // TargetMachine describes the requested code-generation target. Do not
    // populate target capabilities from the host: DGTL must remain correct
    // when cross-compiling and when a JIT target is explicitly selected.
    description_ = TargetDescription::for_arch(m_arch, requested_calling_convention_);
}


} // namespace dgtl::core

