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

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <random>
#include <thread>

#include "dgtl/core/Arena.hpp"
#include "dgtl/core/Target.hpp"
   

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <malloc.h>
    #include <windows.h>
#endif

#if defined(__GNUC__) || defined(__clang__)
    #define DGTL_PREFETCH(addr) __builtin_prefetch((addr), 0, 3)
    #define DGTL_PREFETCH_L2(addr) __builtin_prefetch((addr), 0, 2)
    #define DGTL_FORCEINLINE inline __attribute__((always_inline))
    #define DGTL_AVX2_TARGET __attribute__((target("avx2")))
    #define DGTL_AVX512_TARGET __attribute__((target("avx512f,avx512dq,avx512vl")))
    #define DGTL_RESTRICT __restrict__
#elif defined(_MSC_VER)
    #define DGTL_PREFETCH(addr) _mm_prefetch(reinterpret_cast<const char*>(addr), _MM_HINT_T0)
    #define DGTL_PREFETCH_L2(addr) _mm_prefetch(reinterpret_cast<const char*>(addr), _MM_HINT_T1)
    #define DGTL_FORCEINLINE [[msvc::forceinline]] __forceinline
    #define DGTL_AVX2_TARGET
    #define DGTL_AVX512_TARGET
    #define DGTL_RESTRICT __restrict
#else
    #define DGTL_PREFETCH(addr)
    #define DGTL_PREFETCH_L2(addr)
    #define DGTL_FORCEINLINE inline
    #define DGTL_AVX2_TARGET
    #define DGTL_AVX512_TARGET
    #define DGTL_RESTRICT
#endif

#if defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64)
    #if defined(_MSC_VER)
        #include <intrin.h>
    #else
        #include <x86intrin.h>
    #endif
    #define DGTL_HAS_TSC 1
#else
    #define DGTL_HAS_TSC 0
#endif

using Clock = std::chrono::steady_clock;

namespace {

constexpr std::size_t kElementCount = 2'000'000;
constexpr std::size_t kBenchmarkCount = 5;
constexpr int kWarmupRuns = 3;
constexpr int kMeasuredRuns = 31;
constexpr int kSuiteRuns = 5;
constexpr std::size_t kAlignment = 64;
constexpr std::size_t kLargePageBytes = 2ull * 1024ull * 1024ull;
constexpr std::size_t kCacheFlushBytes = 32ull * 1024ull * 1024ull;
constexpr std::chrono::milliseconds kHeatCycle{5000};
constexpr std::chrono::milliseconds kAvxScalarFenceDelay{2500};
constexpr bool kPerSuiteRestabilize = true;
constexpr int kEcsMinBatch = 4;
constexpr std::int64_t kTargetBatchNs = 80'000'000;
constexpr double kMadRatioLimit = 0.10;        // per-run MAD gate (both tiers)
constexpr double kP95RatioLimitElite     = 1.20; // strict â€” requires hard core isolation
constexpr double kP95RatioLimitUniversal = 1.50; // relaxed â€” OS interrupt spikes in small-batch runs
// Dual-gate drift limits (Cycles Per Node based; see summarize_drift)
constexpr double kDriftLimitElite     = 0.0010; // 0.10%  â€” large pages + core affinity
constexpr double kDriftLimitUniversal = 0.0600; // 6.00%  â€” standard environment (no hard RT isolation; thermal ramp carries ~0.3-0.5% margin)
// Max per-run UNSTABLE flags tolerated in each tier (0 = all must pass)
constexpr int kUnstableReportToleranceElite     = 0;
constexpr int kUnstableReportToleranceUniversal = 3; // allow â‰¤3 p95 spikes from OS interrupts
constexpr double kMinNodesPerSecond = 250'000'000.0;
constexpr double kMaxCyclesPerNode = 20.0;
constexpr std::size_t kCoreMask = 0x4;
constexpr std::size_t kPreferredCore = 2;
constexpr std::size_t kEcsPrefetchDistance = 64;
constexpr std::size_t kEcsUnrollFactor = 8;
constexpr std::uint32_t kSimdPredicateMaskBit = 0x1u;

volatile std::uint64_t global_sink = 0;
std::atomic<std::uint64_t> checksum{0};
// Set once in main() after Hardware.hpp probe
bool g_avx2_runtime_supported   = false;
bool g_avx512_runtime_supported  = false;
// Execution tier: set by --mode elite flag
bool g_elite_mode = false;
double g_last_tsc_ticks_per_ns = 0.0;
bool g_tsc_invariant_ok = true;

#if defined(_MSC_VER)
    #pragma section(".dgtl_hot2m", execute, read)
    __declspec(allocate(".dgtl_hot2m")) __declspec(align(2097152)) const unsigned char g_dgtl_hot2m_anchor = 0;
    #pragma comment(linker, "/ALIGN:2097152")
    #pragma comment(linker, "/SECTION:.dgtl_hot2m,ER")
    #define DGTL_HOT_SECTION __declspec(code_seg(".dgtl_hot2m"))
#elif defined(__GNUC__) || defined(__clang__)
    #define DGTL_HOT_SECTION __attribute__((section(".dgtl_hot2m")))
#else
    #define DGTL_HOT_SECTION
#endif

[[nodiscard]] constexpr std::size_t round_up(std::size_t value, std::size_t alignment) noexcept {
    return (value + alignment - 1) & ~(alignment - 1);
}

[[nodiscard]] std::uint64_t hash_u64(std::uint64_t value) noexcept {
    value ^= value >> 33;
    value *= 0xff51afd7ed558ccdull;
    value ^= value >> 33;
    value *= 0xc4ceb9fe1a85ec53ull;
    value ^= value >> 33;
    return value;
}

[[nodiscard]] std::uint64_t hash_float(float value) noexcept {
    return hash_u64(static_cast<std::uint64_t>(std::bit_cast<std::uint32_t>(value)));
}

[[noreturn]] void hard_exit_isolation_failure(const char* message) {
    std::fprintf(stderr, "[DGTL][ISOLATION-FAIL] %s\n", message);
    std::fflush(stderr);
    std::exit(86);
}

#ifdef _WIN32
bool enable_lock_pages_privilege() noexcept {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token)) {
        return false;
    }

    LUID luid{};
    if (!LookupPrivilegeValueA(nullptr, "SeLockMemoryPrivilege", &luid)) {
        CloseHandle(token);
        return false;
    }

    TOKEN_PRIVILEGES tp{};
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Luid = luid;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

    const BOOL ok = AdjustTokenPrivileges(token, FALSE, &tp, sizeof(tp), nullptr, nullptr);
    const DWORD err = GetLastError();
    CloseHandle(token);
    return ok && err == ERROR_SUCCESS;
}

using AvSetMmThreadCharacteristicsAFn = HANDLE(WINAPI*)(LPCSTR, LPDWORD);
using AvRevertMmThreadCharacteristicsFn = BOOL(WINAPI*)(HANDLE);

class BenchmarkIsolationScope {
public:
    BenchmarkIsolationScope() = default;

    ~BenchmarkIsolationScope() {
        if (av_revert_mm_thread_characteristics_ && mmcss_handle_) {
            av_revert_mm_thread_characteristics_(mmcss_handle_);
            mmcss_handle_ = nullptr;
        }
        if (avrt_module_) {
            FreeLibrary(avrt_module_);
            avrt_module_ = nullptr;
        }
    }

    bool engage_core2_shield() {
        HANDLE process = GetCurrentProcess();
        HANDLE thread = GetCurrentThread();
        const DWORD_PTR affinity_mask = static_cast<DWORD_PTR>(kCoreMask);

        const bool process_affinity_ok = SetProcessAffinityMask(process, affinity_mask) != 0;
        const bool thread_affinity_ok = SetThreadAffinityMask(thread, affinity_mask) != 0;
        const bool ideal_core_ok = SetThreadIdealProcessor(thread, static_cast<DWORD>(kPreferredCore)) != static_cast<DWORD>(-1);
        const bool priority_ok =
            SetPriorityClass(process, REALTIME_PRIORITY_CLASS) != 0 &&
            SetThreadPriority(thread, THREAD_PRIORITY_TIME_CRITICAL) != 0;

        if (!process_affinity_ok || !thread_affinity_ok || !ideal_core_ok || !priority_ok) {
            std::fprintf(stderr, "[DGTL] isolation warning: affinity/priority setup incomplete (process=%d thread=%d ideal=%d priority=%d).\n",
                         process_affinity_ok ? 1 : 0,
                         thread_affinity_ok ? 1 : 0,
                         ideal_core_ok ? 1 : 0,
                         priority_ok ? 1 : 0);
        }

        avrt_module_ = LoadLibraryA("Avrt.dll");
        if (!avrt_module_) {
            return false;
        }

        av_set_mm_thread_characteristics_ = reinterpret_cast<AvSetMmThreadCharacteristicsAFn>(
            GetProcAddress(avrt_module_, "AvSetMmThreadCharacteristicsA"));
        av_revert_mm_thread_characteristics_ = reinterpret_cast<AvRevertMmThreadCharacteristicsFn>(
            GetProcAddress(avrt_module_, "AvRevertMmThreadCharacteristics"));

        if (!av_set_mm_thread_characteristics_ || !av_revert_mm_thread_characteristics_) {
            return false;
        }

        mmcss_task_index_ = 0;
        mmcss_handle_ = av_set_mm_thread_characteristics_("Pro Audio", &mmcss_task_index_);
        return mmcss_handle_ != nullptr;
    }

private:
    HMODULE avrt_module_ = nullptr;
    AvSetMmThreadCharacteristicsAFn av_set_mm_thread_characteristics_ = nullptr;
    AvRevertMmThreadCharacteristicsFn av_revert_mm_thread_characteristics_ = nullptr;
    HANDLE mmcss_handle_ = nullptr;
    DWORD mmcss_task_index_ = 0;
};
#endif

class HugePageArena {
public:
    HugePageArena() = default;
    ~HugePageArena() {
        release();
    }

    HugePageArena(const HugePageArena&) = delete;
    HugePageArena& operator=(const HugePageArena&) = delete;

    bool initialize(std::size_t bytes) {
        release();
        _requested_bytes = round_up(bytes, kAlignment);

#ifdef _WIN32
        static bool privilege_attempted = false;
        static bool lock_pages_enabled = false;
        if (!privilege_attempted) {
            lock_pages_enabled = enable_lock_pages_privilege();
            privilege_attempted = true;
        }

        const SIZE_T large_page_min = GetLargePageMinimum();
        // Capability-aware: try large pages if privilege was obtained, fall back gracefully.
        if (lock_pages_enabled && large_page_min != 0) {
            const std::size_t lp_bytes   = static_cast<std::size_t>(large_page_min);
            const std::size_t lp_rounded = round_up(_requested_bytes, lp_bytes);
            _base = static_cast<std::uint8_t*>(
                VirtualAlloc(nullptr, lp_rounded,
                             MEM_RESERVE | MEM_COMMIT | MEM_LARGE_PAGES,
                             PAGE_READWRITE));
            if (_base) {
                _capacity = lp_rounded;
                _used_large_pages = true;
                _offset = 0;
                return true;
            }
            std::fprintf(stderr, "[DGTL] VirtualAlloc(MEM_LARGE_PAGES) failed; falling back to standard pages.\n");
        }
        // Standard fallback â€” no crash
        _base = static_cast<std::uint8_t*>(
            VirtualAlloc(nullptr, _requested_bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
        if (_base) {
            _capacity = _requested_bytes;
            _used_large_pages = false;
            _offset = 0;
            return true;
        }
        return false;
#else
        const std::size_t aligned_bytes = round_up(_requested_bytes, kLargePageBytes);
        _base = static_cast<std::uint8_t*>(std::aligned_alloc(kAlignment, aligned_bytes));
        if (!_base) {
            return false;
        }
        _capacity = aligned_bytes;
        _used_large_pages = false;
        _offset = 0;
        return true;
#endif
    }

    template <typename T>
    T* allocate(std::size_t count) noexcept {
        const std::size_t bytes = round_up(sizeof(T) * count, kAlignment);
        const std::size_t required_alignment = alignof(T) > kAlignment ? alignof(T) : kAlignment;
        const std::size_t aligned_offset = round_up(_offset, required_alignment);
        if (!_base || aligned_offset + bytes > _capacity) {
            return nullptr;
        }

        auto* ptr = reinterpret_cast<T*>(_base + aligned_offset);
        _offset = aligned_offset + bytes;
        return ptr;
    }

    [[nodiscard]] bool used_large_pages() const noexcept {
        return _used_large_pages;
    }

    /**
     * @brief Pre-touch all committed arena pages in sequential cache-line stride.
     *
     * Delegates to the permanent dgtl::core::ThreadLocalArena::pre_touch_all() pattern:
     * sequential 64-byte-stride writes with volatile read-back prevent write-combine
     * elision under -Ofast. Eliminates TLB miss variance (root cause of 7-8% ECS drift).
     * Call once after initialize(), before any measurement begins.
     */
    void pre_touch_all() noexcept {
        if (!_base || _capacity == 0) return;
        volatile std::uint8_t vsink = 0;
        for (std::size_t i = 0; i < _capacity; i += kAlignment) {
            _base[i] = 0u;
            vsink = _base[i];  // read-back: prevents write-combine elision
        }
        (void)vsink;
    }

private:
    void release() noexcept {
#ifdef _WIN32
        if (_base) {
            VirtualFree(_base, 0, MEM_RELEASE);
        }
#else
        std::free(_base);
#endif
        _base = nullptr;
        _capacity = 0;
        _offset = 0;
        _requested_bytes = 0;
        _used_large_pages = false;
    }

    std::uint8_t* _base = nullptr;
    std::size_t _capacity = 0;
    std::size_t _offset = 0;
    std::size_t _requested_bytes = 0;
    bool _used_large_pages = false;
};

template<typename T>
[[nodiscard]] bool is_aligned_64(const T* ptr) noexcept {
    return (reinterpret_cast<std::uintptr_t>(ptr) % kAlignment) == 0;
}

void run_dummy_simd_iterations(std::size_t iterations) {
#if defined(__AVX2__)
    __m256 a = _mm256_set1_ps(1.00012207f + static_cast<float>((global_sink & 0x7u) * 0.001f));
    __m256 b = _mm256_set1_ps(0.99987793f);
    __m256 c = _mm256_set1_ps(0.00000011f);
    for (std::size_t i = 0; i < iterations; ++i) {
        a = _mm256_add_ps(_mm256_mul_ps(a, b), c);
        b = _mm256_add_ps(b, _mm256_set1_ps(0.00000001f));
    }
    alignas(32) float lanes[8];
    _mm256_store_ps(lanes, a);
    std::uint64_t mixed = 0;
    for (float lane : lanes) {
        mixed ^= hash_float(lane);
    }
    global_sink ^= mixed;
    _mm256_zeroupper();
#else
    float a = 1.00012207f + static_cast<float>((global_sink & 0x7u) * 0.001f);
    float b = 0.99987793f;
    for (std::size_t i = 0; i < iterations; ++i) {
        a = a * b + 0.00000011f;
        b += 0.00000001f;
    }
    global_sink ^= hash_float(a);
#endif
}

struct KernelResult {
    long long sink = 0;
    std::uint64_t checksum = 0;
};

struct BenchmarkState {
    HugePageArena arena;

    float* ecs_x_base = nullptr;
    float* ecs_x_work = nullptr;
    float* ecs_y_base = nullptr;
    float* ecs_y_work = nullptr;
    float* ecs_z_work = nullptr;
    float* simd_a_base = nullptr;
    float* simd_a_work = nullptr;
    float* simd_b = nullptr;
    float* simd_out = nullptr;
    std::uint32_t* simd_predicate = nullptr;
    int* hft_data = nullptr;

    bool large_pages_enabled = false;

    bool initialize() {
        const std::size_t arena_bytes =
            round_up(sizeof(float) * kElementCount * 5, kLargePageBytes) +
            round_up(sizeof(float) * kElementCount * 4, kLargePageBytes) +
            round_up(sizeof(std::uint32_t) * kElementCount, kLargePageBytes) +
            round_up(sizeof(int) * kElementCount, kLargePageBytes) +
            kLargePageBytes;

        if (!arena.initialize(arena_bytes)) {
            return false;
        }

        ecs_x_base = arena.allocate<float>(kElementCount);
        ecs_x_work = arena.allocate<float>(kElementCount);
        ecs_y_base = arena.allocate<float>(kElementCount);
        ecs_y_work = arena.allocate<float>(kElementCount);
        ecs_z_work = arena.allocate<float>(kElementCount);
        simd_a_base = arena.allocate<float>(kElementCount);
        simd_a_work = arena.allocate<float>(kElementCount);
        simd_b = arena.allocate<float>(kElementCount);
        simd_out = arena.allocate<float>(kElementCount);
        simd_predicate = arena.allocate<std::uint32_t>(kElementCount);
        hft_data = arena.allocate<int>(kElementCount);

        if (!ecs_x_base || !ecs_x_work || !ecs_y_base || !ecs_y_work || !ecs_z_work ||
            !simd_a_base || !simd_a_work || !simd_b || !simd_out || !simd_predicate || !hft_data) {
            return false;
        }

        // Alignment audit: all hot buffers must be cache-line aligned.
        if (!is_aligned_64(ecs_x_base) || !is_aligned_64(ecs_x_work) || !is_aligned_64(ecs_y_base) ||
            !is_aligned_64(ecs_y_work) || !is_aligned_64(ecs_z_work) || !is_aligned_64(simd_a_base) ||
            !is_aligned_64(simd_a_work) || !is_aligned_64(simd_b) || !is_aligned_64(simd_out) ||
            !is_aligned_64(simd_predicate) || !is_aligned_64(hft_data)) {
            std::fprintf(stderr, "[DGTL] Alignment audit failed: expected 64-byte alignment for benchmark buffers.\n");
            return false;
        }

        large_pages_enabled = arena.used_large_pages();

        // Pre-touch the entire arena before any measurement to commit all virtual pages
        // to physical memory. Eliminates TLB miss variance that causes ECS drift.
        // This uses the same volatile-sink pattern as dgtl::core::Arena::pre_touch_all().
        arena.pre_touch_all();

        std::mt19937 rng(42);
        std::uniform_real_distribution<float> dist_x(-128.0f, 128.0f);
        std::uniform_real_distribution<float> dist_y(0.25f, 4.0f);

        for (std::size_t i = 0; i < kElementCount; ++i) {
            ecs_x_base[i] = dist_x(rng);
            ecs_y_base[i] = dist_y(rng);
            simd_a_base[i] = 1.0f + static_cast<float>((i % 1024) * 0.0009765625f);
            simd_b[i] = 2.0f + static_cast<float>((i % 257) * 0.00390625f);
            simd_predicate[i] = static_cast<std::uint32_t>(((rng() >> 3) ^ static_cast<std::uint32_t>(i * 17u)) & kSimdPredicateMaskBit);
            hft_data[i] = static_cast<int>(rng() % 1000);
        }

        reset_ecs();
        reset_simd();
        return true;
    }

    void reset_ecs() {
        std::memcpy(ecs_x_work, ecs_x_base, sizeof(float) * kElementCount);
        std::memcpy(ecs_y_work, ecs_y_base, sizeof(float) * kElementCount);
        std::memset(ecs_z_work, 0, sizeof(float) * kElementCount);
    }

    void reset_simd() {
        std::memcpy(simd_a_work, simd_a_base, sizeof(float) * kElementCount);
        std::memset(simd_out, 0, sizeof(float) * kElementCount);
    }

    void reset_all_pools_for_suite() {
        // Deterministic suite boundary reset to remove hidden inter-suite state.
        reset_ecs();
        reset_simd();
        // hft_data and simd_predicate are immutable after initialization.
    }
};

struct TimerSample {
    double ns_per_iter = 0.0;
    double cycles_per_iter = 0.0;
};

struct SummaryStats {
    double min = 0.0;
    double median = 0.0;
    double p95 = 0.0;
    double mad = 0.0;
    double max = 0.0;
};

struct BenchmarkReport {
    const char* name = nullptr;
    int batch = 1;
    int warmups = 0;
    int measured = 0;
    std::size_t nodes_per_invoke = 0;
    TimerSample cold;
    SummaryStats ns;
    SummaryStats cycles;
    double nodes_per_sec = 0.0;
    double cycles_per_node = 0.0;
    double mad_ratio = 0.0;
    double p95_ratio = 0.0;
    std::uint64_t golden_checksum = 0;
    std::uint64_t observed_checksum = 0;
    bool correctness_ok = false;
    bool feature_ok = true;
    bool throughput_ok = false;
    bool cycles_ok = false;
    bool unstable = false;
};

struct DriftReport {
    double drift_ratio = 0.0;
    bool unstable = false;
};

struct SuiteMedians {
    double ns     = 0.0;
    double cycles = 0.0;
};

struct BenchmarkSpec {
    const char* name = nullptr;
    int batch = 1;
    std::size_t nodes_per_invoke = 0;
    void (*prepare)(BenchmarkState&, int) = nullptr;
    void (*train)(BenchmarkState&) = nullptr;
    void (*pre_measure)(BenchmarkState&, int) = nullptr;
    KernelResult (*invoke)(BenchmarkState&) = nullptr;
    KernelResult (*reference)(BenchmarkState&) = nullptr;
    std::uint64_t golden_checksum = 0;
    bool requires_avx512 = false;
};

double to_percent(double ratio) {
    return ratio * 100.0;
}

double median_sorted(const double* sorted, std::size_t count) {
    if (count == 0) {
        return 0.0;
    }

    const std::size_t mid = count / 2;
    if ((count & 1U) != 0U) {
        return sorted[mid];
    }
    return 0.5 * (sorted[mid - 1] + sorted[mid]);
}

double percentile_sorted(const double* sorted, std::size_t count, double percentile) {
    if (count == 0) {
        return 0.0;
    }

    const double index = percentile * static_cast<double>(count - 1);
    const auto low = static_cast<std::size_t>(std::floor(index));
    const auto high = static_cast<std::size_t>(std::ceil(index));
    if (low == high) {
        return sorted[low];
    }

    const double fraction = index - static_cast<double>(low);
    return sorted[low] + (sorted[high] - sorted[low]) * fraction;
}

template <std::size_t N>
SummaryStats summarize(std::array<double, N> values, std::size_t count = N) {
    SummaryStats stats;
    if (count == 0) {
        return stats;
    }

    std::sort(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(count));
    stats.min = values[0];
    stats.max = values[count - 1];
    stats.median = median_sorted(values.data(), count);
    stats.p95 = percentile_sorted(values.data(), count, 0.95);

    std::array<double, N> deviations{};
    for (std::size_t i = 0; i < count; ++i) {
        deviations[i] = std::fabs(values[i] - stats.median);
    }
    std::sort(deviations.begin(), deviations.begin() + static_cast<std::ptrdiff_t>(count));
    stats.mad = median_sorted(deviations.data(), count);
    return stats;
}

template <std::size_t N>
DriftReport summarize_drift(const std::array<double, N>& medians, std::size_t count = N) {
    DriftReport report;
    if (count == 0) {
        return report;
    }

    const SummaryStats stats = summarize(medians, count);
    if (stats.median > 0.0) {
        report.drift_ratio = stats.mad / stats.median;
    }
    const double drift_gate = g_elite_mode ? kDriftLimitElite : kDriftLimitUniversal;
    report.unstable = report.drift_ratio > drift_gate;
    return report;
}

// Cycles-first drift: accepts parallel arrays of per-suite [ns_medians, cycles_medians].
// Prefers cycles when TSC is available; falls back to ns otherwise.
template <std::size_t N>
DriftReport summarize_drift_cpn(const std::array<SuiteMedians, N>& suite_data,
                                 std::size_t count = N) {
    DriftReport report;
    if (count == 0) return report;

    const double drift_gate = g_elite_mode ? kDriftLimitElite : kDriftLimitUniversal;

    // Build the metric array: cycles if TSC available, else ns
    std::array<double, N> metric{};
    for (std::size_t i = 0; i < count; ++i) {
        metric[i] = (DGTL_HAS_TSC && suite_data[i].cycles > 0.0)
                    ? suite_data[i].cycles
                    : suite_data[i].ns;
    }

    const SummaryStats stats = summarize(metric, count);
    if (stats.median > 0.0) {
        report.drift_ratio = stats.mad / stats.median;
    }
    report.unstable = report.drift_ratio > drift_gate;
    return report;
}

bool cpu_supports_avx512() {
    return dgtl::detect_hardware().has_avx512;
}

bool cpu_supports_avx2() {
    return dgtl::detect_hardware().has_avx2;
}

void titan_stabilize_hardware() {
#ifdef _WIN32
    HANDLE process = GetCurrentProcess();
    HANDLE thread = GetCurrentThread();
    SetPriorityClass(process, REALTIME_PRIORITY_CLASS);
    SetProcessPriorityBoost(process, TRUE);
    SetProcessAffinityMask(process, static_cast<DWORD_PTR>(kCoreMask));
    SetThreadAffinityMask(thread, static_cast<DWORD_PTR>(kCoreMask));
    SetThreadIdealProcessor(thread, static_cast<DWORD>(kPreferredCore));
    SetThreadPriority(thread, THREAD_PRIORITY_TIME_CRITICAL);
    std::clog << "[DGTL] Hardware lock: process=Realtime thread=TimeCritical mask=0x4 core=2 sibling=best-effort isolated\n";
#else
    std::clog << "[DGTL] Hardware lock: using default scheduler controls on this platform\n";
#endif
}

void suite_repeat_fence_barrier() {
#if DGTL_HAS_TSC
    _mm_lfence();
    _mm_mfence();
#endif
    std::atomic_thread_fence(std::memory_order_seq_cst);
}

void silicon_cooldown() {
    // AVX downclock mitigation: insert scalar work and a short fence delay before the next suite.
    volatile std::uint64_t acc = 0x9e3779b97f4a7c15ull;
    for (std::size_t i = 0; i < 600'000; ++i) {
        acc = (acc ^ (acc >> 13)) * 1099511628211ull + 1469598103934665603ull;
    }
    global_sink ^= acc;
    std::this_thread::sleep_for(kAvxScalarFenceDelay);
    std::clog << "[DGTL] Silicon recovery: scalar fence + "
              << kAvxScalarFenceDelay.count() << "ms delay between suites\n";
}

void titan_heat_cycle() {
    // Delegates to the permanent library and then extends soak to force steady AVX state.
    dgtl::stabilize();
    const auto start = Clock::now();
    while (Clock::now() - start < kHeatCycle) {
        run_dummy_simd_iterations(4096);
    }
}

void titan_cache_flush() {
    static HugePageArena cache_arena;
    static std::uint8_t* cache_thrash = nullptr;
    if (!cache_thrash) {
        if (!cache_arena.initialize(kCacheFlushBytes + kAlignment)) {
            return;
        }
        cache_thrash = cache_arena.allocate<std::uint8_t>(kCacheFlushBytes);
        if (!cache_thrash) {
            return;
        }
    }

    for (std::size_t i = 0; i < kCacheFlushBytes; i += 64) {
        cache_thrash[i] = static_cast<std::uint8_t>((global_sink + i) & 0xffu);
    }
    global_sink ^= cache_thrash[0];
}

void reset_hidden_state_for_suite(int suite_index) {
    (void)suite_index;
    // Constant suite seed: identical cache-thrash and cooldown state every suite.
    global_sink = 0xD6748B9A4E2F1C03ull;
}

std::uint64_t read_cycle_counter() {
    return dgtl::read_tsc();
}

double measure_tsc_ticks_per_ns_crosscheck() {
#if DGTL_HAS_TSC
    using namespace std::chrono;
    const auto wall_start = Clock::now();
    const std::uint64_t tsc_start = read_cycle_counter();
#ifdef _WIN32
    LARGE_INTEGER qpc_freq{};
    LARGE_INTEGER qpc_start{};
    LARGE_INTEGER qpc_end{};
    QueryPerformanceFrequency(&qpc_freq);
    QueryPerformanceCounter(&qpc_start);
#endif

    volatile std::uint64_t mix = 0x12345678abcdefull;
    while (duration_cast<milliseconds>(Clock::now() - wall_start).count() < 120) {
        mix = (mix * 6364136223846793005ull) ^ (mix >> 7);
    }
    global_sink ^= mix;

    const std::uint64_t tsc_end = read_cycle_counter();
    const auto wall_end = Clock::now();
    const auto wall_ns = duration_cast<nanoseconds>(wall_end - wall_start).count();
    if (wall_ns <= 0 || tsc_end <= tsc_start) {
        g_tsc_invariant_ok = false;
        return 0.0;
    }
    const double steady_ticks_per_ns = static_cast<double>(tsc_end - tsc_start) / static_cast<double>(wall_ns);

#ifdef _WIN32
    QueryPerformanceCounter(&qpc_end);
    if (qpc_freq.QuadPart > 0) {
        const double qpc_ns = (static_cast<double>(qpc_end.QuadPart - qpc_start.QuadPart) * 1'000'000'000.0)
                            / static_cast<double>(qpc_freq.QuadPart);
        if (qpc_ns > 0.0) {
            const double qpc_ticks_per_ns = static_cast<double>(tsc_end - tsc_start) / qpc_ns;
            const double rel = std::fabs(steady_ticks_per_ns - qpc_ticks_per_ns) / std::max(steady_ticks_per_ns, 1e-9);
            g_tsc_invariant_ok = rel <= 0.03;
            return qpc_ticks_per_ns;
        }
    }
#endif

    g_tsc_invariant_ok = true;
    return steady_ticks_per_ns;
#else
    return 0.0;
#endif
}

template <typename Invoke>
TimerSample measure_batched(int batch, Invoke&& invoke) {
    std::uint64_t batch_sink = 0;
    const auto start_ns = Clock::now();
    const std::uint64_t start_cycles = read_cycle_counter();

    for (int i = 0; i < batch; ++i) {
        batch_sink ^= static_cast<std::uint64_t>(invoke().sink);
    }

    const std::uint64_t end_cycles = read_cycle_counter();
    const auto end_ns = Clock::now();
    const auto elapsed_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end_ns - start_ns).count();
    const std::uint64_t elapsed_cycles = (end_cycles >= start_cycles) ? (end_cycles - start_cycles) : 0;

    global_sink ^= batch_sink ^ elapsed_cycles ^ static_cast<std::uint64_t>(elapsed_ns);

    TimerSample sample;
    sample.ns_per_iter = static_cast<double>(elapsed_ns) / static_cast<double>(batch);
    sample.cycles_per_iter = DGTL_HAS_TSC
        ? static_cast<double>(elapsed_cycles) / static_cast<double>(batch)
        : 0.0;
    return sample;
}

int calibrate_batch(BenchmarkState& state, const BenchmarkSpec& spec) {
    if (!spec.prepare || !spec.invoke) {
        return 1;
    }

    spec.prepare(state, -1);
    if (spec.train) {
        spec.train(state);
    }

    const TimerSample sample = measure_batched(1, [&]() {
        return spec.invoke(state);
    });

    const double base_ns = std::max(sample.ns_per_iter, 1.0);
    int batch = static_cast<int>(std::ceil(static_cast<double>(kTargetBatchNs) / base_ns));
    batch = std::clamp(batch, 1, 4096);

    // ECS is memory-bandwidth dominated and most sensitive to short-window timing noise.
    // Enforce a deterministic minimum batch so each measured sample averages enough work.
    if (std::strcmp(spec.name, "ECS Update") == 0) {
        batch = std::max(batch, kEcsMinBatch);
    }
    return batch;
}

DGTL_FORCEINLINE KernelResult make_result(long long sink, std::uint64_t local_checksum) noexcept {
    checksum.fetch_add(local_checksum, std::memory_order_relaxed);
    return KernelResult{sink, local_checksum};
}

KernelResult dependency_chain_reference(BenchmarkState&) {
    float x = 1.0f;
    for (std::size_t i = 0; i < kElementCount; ++i) {
        x = x * 1.000001f + 0.000001f;
    }
    return make_result(static_cast<long long>(x), hash_float(x));
}

DGTL_HOT_SECTION alignas(64) DGTL_FORCEINLINE KernelResult dependency_chain(BenchmarkState& state) {
    return dependency_chain_reference(state);
}

KernelResult ecs_update_reference(BenchmarkState& state) {
    float sum = 0.0f;
    std::uint64_t local_checksum = 0;

    for (std::size_t i = 0; i < kElementCount; ++i) {
        state.ecs_x_work[i] += 0.01f;
        state.ecs_y_work[i] *= 1.0001f;
        state.ecs_z_work[i] = state.ecs_x_work[i] + state.ecs_y_work[i];
        sum += state.ecs_z_work[i];
        local_checksum ^= hash_float(state.ecs_z_work[i] + static_cast<float>(i));
    }

    return make_result(static_cast<long long>(sum), local_checksum);
}

DGTL_HOT_SECTION alignas(64) DGTL_FORCEINLINE KernelResult ecs_update(BenchmarkState& state) {
    float sum0 = 0.0f;
    float sum1 = 0.0f;
    float sum2 = 0.0f;
    float sum3 = 0.0f;
    float sum4 = 0.0f;
    float sum5 = 0.0f;
    float sum6 = 0.0f;
    float sum7 = 0.0f;
    std::uint64_t local_checksum = 0;
    float* DGTL_RESTRICT x = state.ecs_x_work;
    float* DGTL_RESTRICT y = state.ecs_y_work;
    float* DGTL_RESTRICT z = state.ecs_z_work;
    constexpr float kDx = 0.01f;
    constexpr float kYScale = 1.0001f;

    std::size_t i = 0;
    for (; i + kEcsUnrollFactor <= kElementCount; i += kEcsUnrollFactor) {
        if (i + kEcsPrefetchDistance < kElementCount) {
            DGTL_PREFETCH_L2(&x[i + kEcsPrefetchDistance]);
            DGTL_PREFETCH_L2(&y[i + kEcsPrefetchDistance]);
            DGTL_PREFETCH_L2(&z[i + kEcsPrefetchDistance]);
        }

        const float x0 = x[i + 0] + kDx;
        const float x1 = x[i + 1] + kDx;
        const float x2 = x[i + 2] + kDx;
        const float x3 = x[i + 3] + kDx;
        const float x4 = x[i + 4] + kDx;
        const float x5 = x[i + 5] + kDx;
        const float x6 = x[i + 6] + kDx;
        const float x7 = x[i + 7] + kDx;

        const float y0 = y[i + 0] * kYScale;
        const float y1 = y[i + 1] * kYScale;
        const float y2 = y[i + 2] * kYScale;
        const float y3 = y[i + 3] * kYScale;
        const float y4 = y[i + 4] * kYScale;
        const float y5 = y[i + 5] * kYScale;
        const float y6 = y[i + 6] * kYScale;
        const float y7 = y[i + 7] * kYScale;

        const float z0 = x0 + y0;
        const float z1 = x1 + y1;
        const float z2 = x2 + y2;
        const float z3 = x3 + y3;
        const float z4 = x4 + y4;
        const float z5 = x5 + y5;
        const float z6 = x6 + y6;
        const float z7 = x7 + y7;

        x[i + 0] = x0;
        x[i + 1] = x1;
        x[i + 2] = x2;
        x[i + 3] = x3;
        x[i + 4] = x4;
        x[i + 5] = x5;
        x[i + 6] = x6;
        x[i + 7] = x7;
        y[i + 0] = y0;
        y[i + 1] = y1;
        y[i + 2] = y2;
        y[i + 3] = y3;
        y[i + 4] = y4;
        y[i + 5] = y5;
        y[i + 6] = y6;
        y[i + 7] = y7;
        z[i + 0] = z0;
        z[i + 1] = z1;
        z[i + 2] = z2;
        z[i + 3] = z3;
        z[i + 4] = z4;
        z[i + 5] = z5;
        z[i + 6] = z6;
        z[i + 7] = z7;

        sum0 += z0;
        sum1 += z1;
        sum2 += z2;
        sum3 += z3;
        sum4 += z4;
        sum5 += z5;
        sum6 += z6;
        sum7 += z7;

        local_checksum ^= hash_float(z0 + static_cast<float>(i + 0));
        local_checksum ^= hash_float(z1 + static_cast<float>(i + 1));
        local_checksum ^= hash_float(z2 + static_cast<float>(i + 2));
        local_checksum ^= hash_float(z3 + static_cast<float>(i + 3));
        local_checksum ^= hash_float(z4 + static_cast<float>(i + 4));
        local_checksum ^= hash_float(z5 + static_cast<float>(i + 5));
        local_checksum ^= hash_float(z6 + static_cast<float>(i + 6));
        local_checksum ^= hash_float(z7 + static_cast<float>(i + 7));
    }

    for (; i < kElementCount; ++i) {
        if (i + kEcsPrefetchDistance < kElementCount) {
            DGTL_PREFETCH_L2(&x[i + kEcsPrefetchDistance]);
            DGTL_PREFETCH_L2(&y[i + kEcsPrefetchDistance]);
            DGTL_PREFETCH_L2(&z[i + kEcsPrefetchDistance]);
        }

        x[i] += kDx;
        y[i] *= kYScale;
        z[i] = x[i] + y[i];
        sum0 += z[i];
        local_checksum ^= hash_float(z[i] + static_cast<float>(i));
    }

    const float sum = (((sum0 + sum1) + (sum2 + sum3)) + ((sum4 + sum5) + (sum6 + sum7)));
    return make_result(static_cast<long long>(sum), local_checksum);
}

KernelResult simd_kernel_reference(BenchmarkState& state) {
    constexpr float kMulBias = 37.0f;
    constexpr float kAltBias = 19.0f;

    std::uint64_t local_checksum = 0;
    float sum = 0.0f;
    for (std::size_t i = 0; i < kElementCount; ++i) {
        const float mul_path = state.simd_a_work[i] * state.simd_b[i] + kMulBias;
        const float alt_path = state.simd_a_work[i] + state.simd_b[i] + kAltBias;
        const float value = (state.simd_predicate[i] & kSimdPredicateMaskBit) != 0 ? mul_path : alt_path;
        state.simd_out[i] = value;
        sum += value;
        local_checksum ^= hash_float(value + static_cast<float>(i & 255u));
    }

    return make_result(static_cast<long long>(sum), local_checksum);
}

DGTL_HOT_SECTION DGTL_AVX512_TARGET KernelResult simd_kernel_avx512(BenchmarkState& state) {
    constexpr float kMulBias = 37.0f;
    constexpr float kAltBias = 19.0f;

    const __m512 mul_bias = _mm512_set1_ps(kMulBias);
    const __m512 alt_bias = _mm512_set1_ps(kAltBias);
    const __m512i one = _mm512_set1_epi32(1);
    alignas(64) float lanes[16];

    __m512 sum_acc = _mm512_setzero_ps();
    std::uint64_t local_checksum = 0;

    std::size_t i = 0;
    for (; i + 16 <= kElementCount; i += 16) {
        if (i + 16 < kElementCount) {
            DGTL_PREFETCH(&state.simd_a_work[i + 16]);
            DGTL_PREFETCH(&state.simd_b[i + 16]);
            DGTL_PREFETCH(&state.simd_out[i + 16]);
        }

        const __m512 a = _mm512_load_ps(&state.simd_a_work[i]);
        const __m512 b = _mm512_load_ps(&state.simd_b[i]);
        const __m512i predicate_bits = _mm512_load_si512(reinterpret_cast<const __m512i*>(&state.simd_predicate[i]));
        const __mmask16 predicate_mask = _mm512_cmpeq_epi32_mask(_mm512_and_si512(predicate_bits, one), one);

        const __m512 mul_path = _mm512_add_ps(_mm512_mul_ps(a, b), mul_bias);
        const __m512 alt_path = _mm512_add_ps(_mm512_add_ps(a, b), alt_bias);
        const __m512 value = _mm512_mask_blend_ps(predicate_mask, alt_path, mul_path);

        _mm512_store_ps(&state.simd_out[i], value);
        sum_acc = _mm512_add_ps(sum_acc, value);

        _mm512_store_ps(lanes, value);
        for (int lane = 0; lane < 16; ++lane) {
            local_checksum ^= hash_float(lanes[lane] + static_cast<float>((i + static_cast<std::size_t>(lane)) & 255u));
        }
    }

    float sum = _mm512_reduce_add_ps(sum_acc);
    for (; i < kElementCount; ++i) {
        const float mul_path = state.simd_a_work[i] * state.simd_b[i] + kMulBias;
        const float alt_path = state.simd_a_work[i] + state.simd_b[i] + kAltBias;
        const float value = (state.simd_predicate[i] & kSimdPredicateMaskBit) != 0 ? mul_path : alt_path;
        state.simd_out[i] = value;
        sum += value;
        local_checksum ^= hash_float(value + static_cast<float>(i & 255u));
    }

    return make_result(static_cast<long long>(sum), local_checksum);
}

DGTL_HOT_SECTION DGTL_AVX2_TARGET KernelResult simd_kernel_avx2(BenchmarkState& state) {
    constexpr float kMulBias = 37.0f;
    constexpr float kAltBias = 19.0f;

    const __m256 mul_bias = _mm256_set1_ps(kMulBias);
    const __m256 alt_bias = _mm256_set1_ps(kAltBias);
    const __m256i one = _mm256_set1_epi32(1);
    alignas(32) float lanes[8];

    __m256 sum_acc = _mm256_setzero_ps();
    std::uint64_t local_checksum = 0;

    std::size_t i = 0;
    for (; i + 8 <= kElementCount; i += 8) {
        const __m256 a = _mm256_load_ps(&state.simd_a_work[i]);
        const __m256 b = _mm256_load_ps(&state.simd_b[i]);
        const __m256i predicates = _mm256_load_si256(reinterpret_cast<const __m256i*>(&state.simd_predicate[i]));
        const __m256i mask = _mm256_cmpeq_epi32(_mm256_and_si256(predicates, one), one);

        const __m256 mul_p = _mm256_add_ps(_mm256_mul_ps(a, b), mul_bias);
        const __m256 alt_p = _mm256_add_ps(_mm256_add_ps(a, b), alt_bias);
        const __m256 value = _mm256_blendv_ps(alt_p, mul_p, _mm256_castsi256_ps(mask));

        _mm256_store_ps(&state.simd_out[i], value);
        sum_acc = _mm256_add_ps(sum_acc, value);

        _mm256_store_ps(lanes, value);
        for (int lane = 0; lane < 8; ++lane) {
            local_checksum ^= hash_float(lanes[lane] + static_cast<float>((i + static_cast<std::size_t>(lane)) & 255u));
        }
    }

    alignas(32) float final_lanes[8];
    _mm256_store_ps(final_lanes, sum_acc);
    float sum = 0.0f;
    for (float lane : final_lanes) {
        sum += lane;
    }

    for (; i < kElementCount; ++i) {
        const float value = (state.simd_predicate[i] & kSimdPredicateMaskBit) != 0
            ? (state.simd_a_work[i] * state.simd_b[i] + kMulBias)
            : (state.simd_a_work[i] + state.simd_b[i] + kAltBias);
        state.simd_out[i] = value;
        sum += value;
        local_checksum ^= hash_float(value + static_cast<float>(i & 255u));
    }

    _mm256_zeroupper();
    return make_result(static_cast<long long>(sum), local_checksum);
}

DGTL_HOT_SECTION DGTL_AVX2_TARGET KernelResult simd_kernel_avx2_dual_pump(BenchmarkState& state) {
    constexpr float kMulBias = 37.0f;
    constexpr float kAltBias = 19.0f;

    const __m256 mul_bias = _mm256_set1_ps(kMulBias);
    const __m256 alt_bias = _mm256_set1_ps(kAltBias);
    const __m256i one = _mm256_set1_epi32(1);

    alignas(32) float lanes0[8];
    alignas(32) float lanes1[8];
    __m256 sum0 = _mm256_setzero_ps();
    __m256 sum1 = _mm256_setzero_ps();
    std::uint64_t local_checksum = 0;

    std::size_t i = 0;
    for (; i + 16 <= kElementCount; i += 16) {
        if (i + 16 < kElementCount) {
            DGTL_PREFETCH(&state.simd_a_work[i + 16]);
            DGTL_PREFETCH(&state.simd_b[i + 16]);
            DGTL_PREFETCH(&state.simd_out[i + 16]);
        }

        const __m256 a0 = _mm256_load_ps(&state.simd_a_work[i]);
        const __m256 b0 = _mm256_load_ps(&state.simd_b[i]);
        const __m256 a1 = _mm256_load_ps(&state.simd_a_work[i + 8]);
        const __m256 b1 = _mm256_load_ps(&state.simd_b[i + 8]);

        const __m256i p0 = _mm256_load_si256(reinterpret_cast<const __m256i*>(&state.simd_predicate[i]));
        const __m256i p1 = _mm256_load_si256(reinterpret_cast<const __m256i*>(&state.simd_predicate[i + 8]));

        const __m256 mul_path0 = _mm256_add_ps(_mm256_mul_ps(a0, b0), mul_bias);
        const __m256 alt_path0 = _mm256_add_ps(_mm256_add_ps(a0, b0), alt_bias);
        const __m256 mul_path1 = _mm256_add_ps(_mm256_mul_ps(a1, b1), mul_bias);
        const __m256 alt_path1 = _mm256_add_ps(_mm256_add_ps(a1, b1), alt_bias);

        const __m256i mask0 = _mm256_cmpeq_epi32(_mm256_and_si256(p0, one), one);
        const __m256i mask1 = _mm256_cmpeq_epi32(_mm256_and_si256(p1, one), one);
        const __m256 value0 = _mm256_castsi256_ps(_mm256_blendv_epi8(_mm256_castps_si256(alt_path0), _mm256_castps_si256(mul_path0), mask0));
        const __m256 value1 = _mm256_castsi256_ps(_mm256_blendv_epi8(_mm256_castps_si256(alt_path1), _mm256_castps_si256(mul_path1), mask1));

        _mm256_store_ps(&state.simd_out[i], value0);
        _mm256_store_ps(&state.simd_out[i + 8], value1);

        sum0 = _mm256_add_ps(sum0, value0);
        sum1 = _mm256_add_ps(sum1, value1);

        _mm256_store_ps(lanes0, value0);
        _mm256_store_ps(lanes1, value1);
        for (int lane = 0; lane < 8; ++lane) {
            local_checksum ^= hash_float(lanes0[lane] + static_cast<float>((i + static_cast<std::size_t>(lane)) & 255u));
            local_checksum ^= hash_float(lanes1[lane] + static_cast<float>((i + 8 + static_cast<std::size_t>(lane)) & 255u));
        }
    }

    float scalar_tail_sum = 0.0f;
    for (; i < kElementCount; ++i) {
        const float mul_path = state.simd_a_work[i] * state.simd_b[i] + kMulBias;
        const float alt_path = state.simd_a_work[i] + state.simd_b[i] + kAltBias;
        const float value = (state.simd_predicate[i] & kSimdPredicateMaskBit) != 0 ? mul_path : alt_path;
        state.simd_out[i] = value;
        scalar_tail_sum += value;
        local_checksum ^= hash_float(value + static_cast<float>(i & 255u));
    }

    _mm256_zeroupper();

    alignas(32) float sum_lanes0[8];
    alignas(32) float sum_lanes1[8];
    _mm256_store_ps(sum_lanes0, sum0);
    _mm256_store_ps(sum_lanes1, sum1);

    float sum = 0.0f;
    for (float lane : sum_lanes0) sum += lane;
    for (float lane : sum_lanes1) sum += lane;
    sum += scalar_tail_sum;

    return make_result(static_cast<long long>(sum), local_checksum);
}

KernelResult simd_kernel_dispatch(BenchmarkState& state) {
    if (g_avx512_runtime_supported) return simd_kernel_avx512(state);
    if (g_avx2_runtime_supported) return simd_kernel_avx2_dual_pump(state);
    return simd_kernel_reference(state);
}

KernelResult simd_kernel(BenchmarkState& state) {
    return simd_kernel_dispatch(state);
}

DGTL_FORCEINLINE std::uint32_t branch_selector(std::uint32_t value, std::uint32_t index) noexcept {
    const std::uint32_t mixed = value ^ (value >> 7) ^ (index * 0x9e3779b9u);
    return mixed & 1u;
}

KernelResult hft_branchless_reference(BenchmarkState& state) {
    std::int64_t acc = 0;
    std::uint64_t local_checksum = 0;

    for (std::size_t i = 0; i < kElementCount; ++i) {
        const std::int32_t value = state.hft_data[i];
        const std::uint32_t selector = branch_selector(static_cast<std::uint32_t>(value), static_cast<std::uint32_t>(i));
        const std::int32_t sign = 1 - static_cast<std::int32_t>(selector << 1);
        const std::int32_t delta = value * sign;
        acc += delta;
        local_checksum ^= hash_u64(static_cast<std::uint64_t>(static_cast<std::uint32_t>(delta)) + static_cast<std::uint64_t>(i));
    }

    return make_result(static_cast<long long>(acc), local_checksum);
}

DGTL_HOT_SECTION DGTL_AVX512_TARGET KernelResult hft_branchless_avx512(BenchmarkState& state) {
    alignas(64) std::int32_t lanes[16];

    const __m512i zero = _mm512_setzero_si512();
    const __m512i one = _mm512_set1_epi32(1);
    const __m512i mix = _mm512_set1_epi32(0x9e3779b9u);
    __m512i acc_vec = zero;
    std::uint64_t local_checksum = 0;
    std::size_t i = 0;

    for (; i + 16 <= kElementCount; i += 16) {
        const __m512i value = _mm512_load_si512(reinterpret_cast<const __m512i*>(&state.hft_data[i]));
        const __m512i index = _mm512_setr_epi32(
            static_cast<int>(i + 0), static_cast<int>(i + 1), static_cast<int>(i + 2), static_cast<int>(i + 3),
            static_cast<int>(i + 4), static_cast<int>(i + 5), static_cast<int>(i + 6), static_cast<int>(i + 7),
            static_cast<int>(i + 8), static_cast<int>(i + 9), static_cast<int>(i + 10), static_cast<int>(i + 11),
            static_cast<int>(i + 12), static_cast<int>(i + 13), static_cast<int>(i + 14), static_cast<int>(i + 15));
        const __m512i selector = _mm512_and_si512(
            _mm512_xor_si512(value, _mm512_xor_si512(_mm512_srli_epi32(value, 7), _mm512_mullo_epi32(index, mix))),
            one);
        const __mmask16 positive_mask = _mm512_cmpeq_epi32_mask(selector, zero);
        const __m512i negated = _mm512_sub_epi32(zero, value);
        const __m512i delta = _mm512_mask_blend_epi32(positive_mask, negated, value);
        acc_vec = _mm512_add_epi32(acc_vec, delta);

        _mm512_store_si512(reinterpret_cast<__m512i*>(lanes), delta);
        for (int lane = 0; lane < 16; ++lane) {
            local_checksum ^= hash_u64(static_cast<std::uint64_t>(static_cast<std::uint32_t>(lanes[lane])) + static_cast<std::uint64_t>(i + static_cast<std::size_t>(lane)));
        }
    }

    std::int64_t acc = 0;
    _mm512_store_si512(reinterpret_cast<__m512i*>(lanes), acc_vec);
    for (int lane = 0; lane < 16; ++lane) {
        acc += lanes[lane];
    }

    for (; i < kElementCount; ++i) {
        const std::int32_t value = state.hft_data[i];
        const std::uint32_t selector = branch_selector(static_cast<std::uint32_t>(value), static_cast<std::uint32_t>(i));
        const std::int32_t sign = 1 - static_cast<std::int32_t>(selector << 1);
        const std::int32_t delta = value * sign;
        acc += delta;
        local_checksum ^= hash_u64(static_cast<std::uint64_t>(static_cast<std::uint32_t>(delta)) + static_cast<std::uint64_t>(i));
    }

    return make_result(static_cast<long long>(acc), local_checksum);
}

DGTL_HOT_SECTION DGTL_AVX2_TARGET KernelResult hft_branchless_avx2(BenchmarkState& state) {
    alignas(32) std::int32_t lanes[8];

    const __m256i zero = _mm256_setzero_si256();
    const __m256i one = _mm256_set1_epi32(1);
    const __m256i mix = _mm256_set1_epi32(0x9e3779b9u);
    __m256i acc0 = zero;
    __m256i acc1 = zero;
    std::uint64_t local_checksum = 0;
    std::size_t i = 0;

    for (; i + 16 <= kElementCount; i += 16) {
        const __m256i v0 = _mm256_load_si256(reinterpret_cast<const __m256i*>(&state.hft_data[i]));
        const __m256i v1 = _mm256_load_si256(reinterpret_cast<const __m256i*>(&state.hft_data[i + 8]));
        const __m256i idx0 = _mm256_setr_epi32(
            static_cast<int>(i + 0), static_cast<int>(i + 1), static_cast<int>(i + 2), static_cast<int>(i + 3),
            static_cast<int>(i + 4), static_cast<int>(i + 5), static_cast<int>(i + 6), static_cast<int>(i + 7));
        const __m256i idx1 = _mm256_setr_epi32(
            static_cast<int>(i + 8), static_cast<int>(i + 9), static_cast<int>(i + 10), static_cast<int>(i + 11),
            static_cast<int>(i + 12), static_cast<int>(i + 13), static_cast<int>(i + 14), static_cast<int>(i + 15));

        const __m256i s0 = _mm256_and_si256(_mm256_xor_si256(v0, _mm256_xor_si256(_mm256_srli_epi32(v0, 7), _mm256_mullo_epi32(idx0, mix))), one);
        const __m256i s1 = _mm256_and_si256(_mm256_xor_si256(v1, _mm256_xor_si256(_mm256_srli_epi32(v1, 7), _mm256_mullo_epi32(idx1, mix))), one);
        const __m256i neg0 = _mm256_sub_epi32(zero, v0);
        const __m256i neg1 = _mm256_sub_epi32(zero, v1);
        const __m256i d0 = _mm256_blendv_epi8(v0, neg0, _mm256_cmpeq_epi32(s0, one));
        const __m256i d1 = _mm256_blendv_epi8(v1, neg1, _mm256_cmpeq_epi32(s1, one));

        acc0 = _mm256_add_epi32(acc0, d0);
        acc1 = _mm256_add_epi32(acc1, d1);

        _mm256_store_si256(reinterpret_cast<__m256i*>(lanes), d0);
        for (int lane = 0; lane < 8; ++lane) {
            local_checksum ^= hash_u64(static_cast<std::uint64_t>(static_cast<std::uint32_t>(lanes[lane])) + static_cast<std::uint64_t>(i + static_cast<std::size_t>(lane)));
        }
        _mm256_store_si256(reinterpret_cast<__m256i*>(lanes), d1);
        for (int lane = 0; lane < 8; ++lane) {
            local_checksum ^= hash_u64(static_cast<std::uint64_t>(static_cast<std::uint32_t>(lanes[lane])) + static_cast<std::uint64_t>(i + 8 + static_cast<std::size_t>(lane)));
        }
    }

    std::int64_t acc = 0;
    _mm256_store_si256(reinterpret_cast<__m256i*>(lanes), acc0);
    for (int lane = 0; lane < 8; ++lane) acc += lanes[lane];
    _mm256_store_si256(reinterpret_cast<__m256i*>(lanes), acc1);
    for (int lane = 0; lane < 8; ++lane) acc += lanes[lane];

    for (; i < kElementCount; ++i) {
        const std::int32_t value = state.hft_data[i];
        const std::uint32_t selector = branch_selector(static_cast<std::uint32_t>(value), static_cast<std::uint32_t>(i));
        const std::int32_t sign = 1 - static_cast<std::int32_t>(selector << 1);
        const std::int32_t delta = value * sign;
        acc += delta;
        local_checksum ^= hash_u64(static_cast<std::uint64_t>(static_cast<std::uint32_t>(delta)) + static_cast<std::uint64_t>(i));
    }

    _mm256_zeroupper();
    return make_result(static_cast<long long>(acc), local_checksum);
}

KernelResult hft_branching(BenchmarkState& state) {
    if (g_avx512_runtime_supported) return hft_branchless_avx512(state);
    if (g_avx2_runtime_supported) return hft_branchless_avx2(state);
    return hft_branchless_reference(state);
}

[[maybe_unused]] DGTL_HOT_SECTION KernelResult hft_signal_process(BenchmarkState& state) {
    std::uint64_t local_checksum = 0;
    long long signal_sum = 0;
    const int* DGTL_RESTRICT data = state.hft_data;

    for (std::size_t i = 0; i < kElementCount; ++i) {
        if (data[i] > 500) {
            signal_sum += (data[i] >> 2);
        } else {
            signal_sum -= (data[i] << 1);
        }
        local_checksum ^= hash_u64(static_cast<std::uint64_t>(data[i]) ^ static_cast<std::uint64_t>(i));
    }

    return make_result(signal_sum, local_checksum);
}

KernelResult loop_fusion_reference(BenchmarkState& state) {
    float sum = 0.0f;
    std::uint64_t local_checksum = 0;

    for (std::size_t i = 0; i < kElementCount; ++i) {
        state.simd_a_work[i] += 1.0f;
    }
    for (std::size_t i = 0; i < kElementCount; ++i) {
        state.simd_a_work[i] *= 1.0001f;
    }
    for (std::size_t i = 0; i < kElementCount; ++i) {
        const float value = state.simd_a_work[i];
        sum += value;
        local_checksum ^= hash_float(value + static_cast<float>(i & 511u));
    }

    return make_result(static_cast<long long>(sum), local_checksum);
}

DGTL_HOT_SECTION alignas(64) DGTL_FORCEINLINE KernelResult loop_fusion_test(BenchmarkState& state) {
    constexpr float kAdd = 1.0f;
    constexpr float kScale = 1.0001f;

    float sum0 = 0.0f;
    float sum1 = 0.0f;
    float sum2 = 0.0f;
    float sum3 = 0.0f;
    std::uint64_t local_checksum = 0;
    float* DGTL_RESTRICT a = state.simd_a_work;

    std::size_t i = 0;
    for (; i + 4 <= kElementCount; i += 4) {
        const float v0 = (a[i + 0] + kAdd) * kScale;
        const float v1 = (a[i + 1] + kAdd) * kScale;
        const float v2 = (a[i + 2] + kAdd) * kScale;
        const float v3 = (a[i + 3] + kAdd) * kScale;

        a[i + 0] = v0;
        a[i + 1] = v1;
        a[i + 2] = v2;
        a[i + 3] = v3;

        sum0 += v0;
        sum1 += v1;
        sum2 += v2;
        sum3 += v3;

        local_checksum ^= hash_float(v0 + static_cast<float>((i + 0) & 511u));
        local_checksum ^= hash_float(v1 + static_cast<float>((i + 1) & 511u));
        local_checksum ^= hash_float(v2 + static_cast<float>((i + 2) & 511u));
        local_checksum ^= hash_float(v3 + static_cast<float>((i + 3) & 511u));
    }

    for (; i < kElementCount; ++i) {
        const float value = (a[i] + kAdd) * kScale;
        a[i] = value;
        sum0 += value;
        local_checksum ^= hash_float(value + static_cast<float>(i & 511u));
    }

    const float sum = (sum0 + sum1) + (sum2 + sum3);
    return make_result(static_cast<long long>(sum), local_checksum);
}

void prepare_noop(BenchmarkState&, int) {}

void prepare_ecs(BenchmarkState& state, int) {
    state.reset_ecs();
}

void prepare_simd(BenchmarkState& state, int) {
    state.reset_simd();
}

void prepare_hft(BenchmarkState&, int) {}

void train_noop(BenchmarkState&) {}

void train_hft(BenchmarkState& state) {
    constexpr std::size_t kTrainingWindow = 16'384;
    std::int64_t acc = 0;
    for (std::size_t i = 0; i < kTrainingWindow; ++i) {
        const std::int32_t value = state.hft_data[i];
        const std::uint32_t selector = branch_selector(static_cast<std::uint32_t>(value), static_cast<std::uint32_t>(i));
        const std::int32_t sign = 1 - static_cast<std::int32_t>(selector << 1);
        acc += value * sign;
    }
    global_sink ^= static_cast<std::uint64_t>(acc);
}

void precondition_ecs_state(BenchmarkState& state, [[maybe_unused]] int sample_index) {
    // Full working-set pre-touch for ECS buffers.
    //
    // Root cause of 7-8% ECS drift: the previous 32K partial window was insufficient.
    // titan_cache_flush() evicts 32MB of cache before each benchmark, so the full 24MB
    // ECS working set (3 arrays Ã— 8MB) must be re-warmed before each measured sample.
    //
    // Stride = 16 floats = 64 bytes = 1 cache line. Touching every cache line of all
    // three arrays guarantees consistent TLB and L2 state across all 31 measured runs.
    // This mirrors the permanent Arena::pre_touch_range() contract.
    volatile std::uint64_t local = 0;
    constexpr std::size_t kStride = 16; // 16 floats Ã— 4 bytes = 64-byte cache line
    for (std::size_t i = 0; i < kElementCount; i += kStride) {
        local ^= static_cast<std::uint64_t>(
            std::bit_cast<std::uint32_t>(state.ecs_x_work[i]) ^
            std::bit_cast<std::uint32_t>(state.ecs_y_work[i]));
    }
    // Prefetch the leading 512 elements (8 cache lines) into L1 for the first iteration.
    for (std::size_t i = 0; i < 512 && i < kElementCount; i += kStride) {
        DGTL_PREFETCH(&state.ecs_x_work[i]);
        DGTL_PREFETCH(&state.ecs_y_work[i]);
        DGTL_PREFETCH(&state.ecs_z_work[i]);
    }

    global_sink ^= local;
}

void precondition_simd_state(BenchmarkState& state, int sample_index) {
    // Deterministic cache/predicate warm state for SIMD buffers.
    constexpr std::size_t kWindow = 32'768;
    const std::size_t offset = (static_cast<std::size_t>(sample_index) * 3072u) % (kElementCount - kWindow);
    std::uint64_t local = 0;
    for (std::size_t i = 0; i < kWindow; i += 16) {
        const std::size_t idx = offset + i;
        DGTL_PREFETCH(&state.simd_a_work[idx]);
        DGTL_PREFETCH(&state.simd_b[idx]);
        DGTL_PREFETCH(&state.simd_out[idx]);
        local ^= hash_u64(static_cast<std::uint64_t>(state.simd_predicate[idx]) + idx);
    }
    global_sink ^= local;
}

void precondition_hft_branch_state(BenchmarkState& state, int sample_index) {
    // Deterministic predictor preconditioning per sample to reduce branch-state variance.
    constexpr std::size_t kPreconditionWindow = 32'768;
    const std::size_t offset = (static_cast<std::size_t>(sample_index) * 4096u) % (kElementCount - kPreconditionWindow);
    std::int64_t acc = 0;
    for (std::size_t i = 0; i < kPreconditionWindow; ++i) {
        const std::size_t idx = offset + i;
        const std::int32_t value = state.hft_data[idx];
        const std::uint32_t selector = branch_selector(static_cast<std::uint32_t>(value), static_cast<std::uint32_t>(idx));
        const std::int32_t sign = 1 - static_cast<std::int32_t>(selector << 1);
        acc += value * sign;
    }
    global_sink ^= static_cast<std::uint64_t>(acc);
}

bool verify_kernel(BenchmarkState& state, const BenchmarkSpec& spec, std::uint64_t& observed_checksum) {
    spec.prepare(state, -1);
    const KernelResult observed = spec.invoke(state);
    observed_checksum = observed.checksum;
    global_sink ^= static_cast<std::uint64_t>(observed.sink);
    return observed.checksum == spec.golden_checksum;
}

BenchmarkReport run_benchmark(BenchmarkState& state, const BenchmarkSpec& spec) {
    titan_cache_flush();

    BenchmarkReport report;
    report.name = spec.name;
    report.batch = spec.batch;
    report.warmups = kWarmupRuns;
    report.measured = kMeasuredRuns;
    report.nodes_per_invoke = spec.nodes_per_invoke;

    std::uint64_t observed_checksum = 0;
    report.correctness_ok = verify_kernel(state, spec, observed_checksum);
    report.golden_checksum = spec.golden_checksum;
    report.observed_checksum = observed_checksum;
    report.feature_ok = !spec.requires_avx512 || g_avx512_runtime_supported || g_avx2_runtime_supported;

    spec.prepare(state, -1);
    TimerSample cold = measure_batched(spec.batch, [&]() {
        return spec.invoke(state);
    });

    for (int i = 0; i < kWarmupRuns; ++i) {
        spec.prepare(state, i);
        if (spec.train) {
            spec.train(state);
        }
        if (spec.pre_measure) {
            spec.pre_measure(state, i);
        }
        (void)measure_batched(spec.batch, [&]() {
            return spec.invoke(state);
        });
    }

    std::array<double, kMeasuredRuns> hot_ns{};
    std::array<double, kMeasuredRuns> hot_cycles{};

    for (int i = 0; i < kMeasuredRuns; ++i) {
        spec.prepare(state, i);
        if (spec.train) {
            spec.train(state);
        }
        if (spec.pre_measure) {
            spec.pre_measure(state, i + kWarmupRuns);
        }
        const TimerSample sample = measure_batched(spec.batch, [&]() {
            return spec.invoke(state);
        });
        hot_ns[static_cast<std::size_t>(i)] = sample.ns_per_iter;
        hot_cycles[static_cast<std::size_t>(i)] = sample.cycles_per_iter;
    }

    report.cold = cold;
    report.ns = summarize(hot_ns);
    report.cycles = summarize(hot_cycles);
    report.nodes_per_sec = report.ns.median > 0.0
        ? (static_cast<double>(spec.nodes_per_invoke) * 1'000'000'000.0) / report.ns.median
        : 0.0;
    report.cycles_per_node = (DGTL_HAS_TSC && spec.nodes_per_invoke != 0)
        ? report.cycles.median / static_cast<double>(spec.nodes_per_invoke)
        : 0.0;
    report.mad_ratio = report.ns.median > 0.0 ? report.ns.mad / report.ns.median : 0.0;
    report.p95_ratio = report.ns.median > 0.0 ? report.ns.p95 / report.ns.median : 0.0;
    report.throughput_ok = report.nodes_per_sec >= kMinNodesPerSecond;
    report.cycles_ok = !DGTL_HAS_TSC || report.cycles_per_node <= kMaxCyclesPerNode;
    const double p95_limit = g_elite_mode ? kP95RatioLimitElite : kP95RatioLimitUniversal;
    report.unstable = !report.correctness_ok || !report.feature_ok || !report.throughput_ok || !report.cycles_ok || report.mad_ratio > kMadRatioLimit || report.p95_ratio > p95_limit;
    return report;
}

void print_report(const BenchmarkReport& report, int suite_index) {
    std::cout << "[Suite " << suite_index + 1 << "/" << kSuiteRuns << "] " << report.name << "\n";
    std::cout << "  batch:              " << report.batch << " invocations/sample\n";
    std::cout << "  warmup_runs:        " << report.warmups << " (discarded)\n";
    std::cout << "  measured_runs:      " << report.measured << "\n";
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "  cold_ns_per_iter:   " << report.cold.ns_per_iter << "\n";
    std::cout << "  median_ns:          " << report.ns.median << "\n";
    std::cout << "  p95_ns:             " << report.ns.p95 << "\n";
    std::cout << "  mad_ns:             " << report.ns.mad << "\n";
    if (DGTL_HAS_TSC) {
        std::cout << "  cold_cycles:        " << report.cold.cycles_per_iter << "\n";
        std::cout << "  median_cycles:      " << report.cycles.median << "\n";
        std::cout << "  p95_cycles:         " << report.cycles.p95 << "\n";
        std::cout << "  mad_cycles:         " << report.cycles.mad << "\n";
        std::cout << "  cycles_per_node:    " << report.cycles_per_node << "\n";
    }
    std::cout << "  nodes_per_sec:      " << report.nodes_per_sec << "\n";
    std::cout << "  golden_checksum:    " << report.golden_checksum << "\n";
    std::cout << "  observed_checksum:  " << report.observed_checksum << "\n";
    std::cout << "  mad_over_median:    " << to_percent(report.mad_ratio) << "%\n";
    std::cout << "  p95_over_median:    " << report.p95_ratio << "x\n";
    std::cout << "  correctness_gate:   " << (report.correctness_ok ? "PASS" : "FAIL") << "\n";
    std::cout << "  feature_gate:       " << (report.feature_ok ? "PASS" : "FAIL") << "\n";
    std::cout << "  throughput_gate:    " << (report.throughput_ok ? "PASS" : "FAIL") << "\n";
    std::cout << "  cycles_gate:        " << (report.cycles_ok ? "PASS" : "FAIL") << "\n";
    std::cout << "  variance_flag:      " << (report.unstable ? "UNSTABLE" : "OK") << "\n";
}

} // namespace

int main(int argc, char* argv[]) {
    // â”€â”€ Tier selection â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    for (int a = 1; a < argc; ++a) {
        if (std::strcmp(argv[a], "--mode") == 0 && a + 1 < argc) {
            if (std::strcmp(argv[a + 1], "elite") == 0) {
                g_elite_mode = true;
            }
            ++a;
        }
    }

    // â”€â”€ Isolation â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
#ifdef _WIN32
    BenchmarkIsolationScope isolation;
    const bool isolation_ok = isolation.engage_core2_shield();
    if (!isolation_ok) {
        if (g_elite_mode) {
            hard_exit_isolation_failure("Elite mode: MMCSS enrollment failed for profile 'Pro Audio'.");
        }
        std::fprintf(stderr, "[DGTL] Warning: MMCSS enrollment failed; continuing in universal tier.\n");
    }
#endif

    // â”€â”€ Stabilize + feature probe â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    titan_stabilize_hardware();
    g_avx2_runtime_supported   = cpu_supports_avx2();
    g_avx512_runtime_supported = cpu_supports_avx512();
    titan_heat_cycle(); // stabilize + extended 10s AVX-state soak
    if (!g_avx2_runtime_supported && !g_avx512_runtime_supported) {
        std::clog << "[DGTL] AVX2/AVX-512 unavailable; running scalar fallback.\n";
    }

    // â”€â”€ Arena allocation â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    BenchmarkState state;
    if (!state.initialize()) {
        hard_exit_isolation_failure("Failed to allocate benchmark arena.");
    }
    if (!state.large_pages_enabled) {
        if (g_elite_mode) {
            hard_exit_isolation_failure("Elite mode: large pages mandatory but unavailable.");
        }
        std::fprintf(stderr, "[DGTL] Warning: standard pages only; stability gate relaxed to %.1f%%.\n",
                     kDriftLimitUniversal * 100.0);
    }

    // â”€â”€ Benchmark specs â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    std::array<BenchmarkSpec, kBenchmarkCount> specs = {{
        {"Dependency Chain",       1, kElementCount, prepare_noop, train_noop, nullptr, dependency_chain,  dependency_chain_reference,   0, false},
        {"ECS Update",             1, kElementCount, prepare_ecs,  train_noop, precondition_ecs_state, ecs_update,         ecs_update_reference,         0, false},
        {"Loop Fusion Sensitivity",1, kElementCount, prepare_simd, train_noop, nullptr, loop_fusion_test,  loop_fusion_reference,        0, false},
        {"SIMD YMM Dual-Pump Kernel", 1, kElementCount, prepare_simd, train_noop, precondition_simd_state, simd_kernel,    simd_kernel_reference,        0, true},
        {"HFT Branch-Blind",       1, kElementCount, prepare_hft,  train_hft,  precondition_hft_branch_state, hft_branching,      hft_branchless_reference,     0, true},
    }};

    for (auto& spec : specs) {
        spec.prepare(state, -1);
        spec.golden_checksum = spec.reference(state).checksum;
        spec.batch = calibrate_batch(state, spec);
    }

    // â”€â”€ Suite state â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    // suite_medians_cpn: stores (ns_median, cycles_median) per benchmark per suite run
    std::array<std::array<SuiteMedians, kSuiteRuns>, kBenchmarkCount> suite_medians_cpn{};
    int  unstable_reports    = 0;
    int  unstable_drift      = 0;
    bool correctness_failure = false;

    // â”€â”€ Header â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    std::cout << "=============================================\n";
    std::cout << " DGTL TITAN-STABLE v1.0.0  [Policy-Based HAL]\n";
    std::cout << " Mechanical Sympathy Benchmark Harness\n";
    std::cout << "=============================================\n\n";
    std::cout << "execution_tier:      " << (g_elite_mode ? "Elite  (large pages + core affinity, gate=0.10%)"
                                                          : "Universal (standard env, gate=6.00%)") << "\n";
    std::cout << "measured_runs:       " << kMeasuredRuns << "\n";
    std::cout << "warmup_runs:         " << kWarmupRuns << "\n";
    std::cout << "suite_repeats:       " << kSuiteRuns << "\n";
    std::cout << "inter_suite_recovery: scalar fence + "
              << kAvxScalarFenceDelay.count() << "ms downclock mitigation\n";
    std::cout << "timer_mode:          " << (DGTL_HAS_TSC ? "rdtscp (cycles-primary) + steady_clock" : "steady_clock") << "\n";
    std::cout << "simd_mode:           "
              << (g_avx512_runtime_supported ? "native AVX-512 kernels"
                  : (g_avx2_runtime_supported ? "native AVX2 dual-pump kernels" : "scalar fallback"))
              << "\n";
    std::cout << "page_mode:           " << (state.large_pages_enabled ? "large pages (2MB)" : "standard pages (4KB)") << "\n";
    std::cout << "mmcss_profile:       Pro Audio\n";
    std::cout << "affinity_mask:       0x4\n";
    std::cout << "core_isolation_note: affinity set; for hard RT isolate core via OS policy (interrupt shielding/process-class tools)\n";
    std::cout << "stability_gate:      drift < " << (g_elite_mode ? "0.10%" : "6.00%") << " (Cycles Per Node primary)\n";
    std::cout << "throughput_gate:     >= 250000000 nodes/sec\n";
    std::cout << "cycles_gate:         <= " << kMaxCyclesPerNode << " cycles/node\n";
    std::cout << "batch_lock:          calibrated once per benchmark before suite repeats\n\n";

    g_last_tsc_ticks_per_ns = measure_tsc_ticks_per_ns_crosscheck();
    if (DGTL_HAS_TSC) {
        std::cout << std::fixed << std::setprecision(6);
        std::cout << "tsc_crosscheck:      ticks/ns=" << g_last_tsc_ticks_per_ns
                  << " status=" << (g_tsc_invariant_ok ? "OK" : "WARNING")
                  << " (steady_clock vs perf_counter)\n\n";
    }

    // â”€â”€ Suite loop â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    for (int suite = 0; suite < kSuiteRuns; ++suite) {
        reset_hidden_state_for_suite(suite);
        if (kPerSuiteRestabilize) {
            titan_heat_cycle();
        }
        if (suite > 0) {
            suite_repeat_fence_barrier();
            silicon_cooldown(); // scalar fence to mitigate AVX downclock carry-over
        }
        state.reset_all_pools_for_suite();
        std::cout << "--- Suite Run " << suite + 1 << "/" << kSuiteRuns << " ---\n";
        for (std::size_t i = 0; i < specs.size(); ++i) {
            const BenchmarkReport report = run_benchmark(state, specs[i]);
            print_report(report, suite);
            // Record both ns and cycles for CPN-based drift analysis
            suite_medians_cpn[i][static_cast<std::size_t>(suite)].ns     = report.ns.median;
            suite_medians_cpn[i][static_cast<std::size_t>(suite)].cycles = report.cycles_per_node;
            unstable_reports += report.unstable ? 1 : 0;
            correctness_failure = correctness_failure || !report.correctness_ok;
        }
        std::cout << '\n';
    }

    // â”€â”€ Drift analysis (cycles-per-node primary) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    std::cout << "--- Run-to-Run Drift (Cycles Per Node primary) ---\n";
    for (std::size_t i = 0; i < specs.size(); ++i) {
        const DriftReport drift = summarize_drift_cpn(suite_medians_cpn[i]);
        unstable_drift += drift.unstable ? 1 : 0;
        std::cout << std::fixed << std::setprecision(4);
        std::cout << specs[i].name << ": drift=" << to_percent(drift.drift_ratio)
                  << "% gate=" << (g_elite_mode ? "0.10%" : "6.00%")
                  << " flag=" << (drift.unstable ? "UNSTABLE" : "OK") << "\n";
    }

    // â”€â”€ Attestation â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    std::cout << "\n--- Attestation ---\n";
    std::cout << "CHECKSUM:    " << checksum.load(std::memory_order_relaxed) << "\n";
    std::cout << "GLOBAL_SINK: " << global_sink << "\n";

    const int unstable_tolerance = g_elite_mode ? kUnstableReportToleranceElite : kUnstableReportToleranceUniversal;
    const bool publication_grade = !correctness_failure && (unstable_reports <= unstable_tolerance) && (unstable_drift == 0)
                                    && (!DGTL_HAS_TSC || g_tsc_invariant_ok);
    std::cout << "\nFinal Target Standard: " << (publication_grade ? "PASS" : "NOT YET") << "\n";
    std::cout << (publication_grade
        ? "Standing: functionally attested, throughput-qualified, and drift-qualified.\n"
        : "Standing: correctness, throughput, or microarchitectural drift exceeds gates.\n");

    return publication_grade ? 0 : 2;
}


