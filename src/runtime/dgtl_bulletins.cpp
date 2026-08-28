/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository: github.com/TataG-Systems/DGTL
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

module;

#include <cstdlib>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
#   ifndef WIN32_LEAN_AND_MEAN
#       define WIN32_LEAN_AND_MEAN
#   endif
#   include <windows.h>
#else
#   include <unistd.h>
#endif

module dgtl.dgtl_bulletins;

import dgtl.flat_map;

namespace dgtl::runtime::detail {

// Module-internal static instances
::dgtl::utils::container::FastFlatMap<uint32_t, std::string, 4096>& string_registry() noexcept {
    static ::dgtl::utils::container::FastFlatMap<uint32_t, std::string, 4096> registry;
    return registry;
}

std::shared_mutex& string_registry_mutex() noexcept {
    static std::shared_mutex mtx;
    return mtx;
}

} // namespace dgtl::runtime::detail

/* ─── THREAD-SAFE STRING REGISTRY IMPLEMENTATION ─── */

namespace dgtl::runtime {

void register_dgtl_string(uint32_t handle, const char* data, uint32_t len) {
    if (!data) return;
    std::unique_lock<std::shared_mutex> lock(detail::string_registry_mutex());
    (void)detail::string_registry().insert(handle, std::string(data, static_cast<std::size_t>(len)));
}

[[nodiscard]] std::string_view lookup_string(uint32_t handle) noexcept {
    std::shared_lock<std::shared_mutex> lock(detail::string_registry_mutex());
    if (const auto* value = detail::string_registry().find(handle)) [[likely]] {
        return *value;
    }
    return "<invalid_fmt_handle>";
}

[[nodiscard]] std::string_view try_lookup_string(uint64_t raw_value) noexcept {
    std::shared_lock<std::shared_mutex> lock(detail::string_registry_mutex());
    const uint32_t low32 = static_cast<uint32_t>(raw_value & 0xFFFFFFFFULL);
    if (const auto* value = detail::string_registry().find(low32)) {
        return *value;
    }
    const uint32_t high32 = static_cast<uint32_t>(raw_value >> 32u);
    if (high32 != 0u) {
        if (const auto* value = detail::string_registry().find(high32)) {
            return *value;
        }
    }
    return {};
}

[[nodiscard]] std::string_view try_lookup_string_handle(uint32_t handle) noexcept {
    return try_lookup_string(static_cast<uint64_t>(handle));
}

[[nodiscard]] std::string_view lookup_string_handle(uint32_t handle) noexcept {
    return lookup_string(handle);
}

void register_string_handle(uint32_t handle, const char* data, uint32_t len) {
    register_dgtl_string(handle, data, len);
}

void unregister_string_handle(uint32_t handle) noexcept {
    std::unique_lock<std::shared_mutex> lock(detail::string_registry_mutex());
    (void)detail::string_registry().erase(handle);
}

[[nodiscard]] size_t get_registered_string_count() noexcept {
    std::shared_lock<std::shared_mutex> lock(detail::string_registry_mutex());
    return detail::string_registry().size();
}

[[nodiscard]] std::vector<uint8_t> build_registered_strings_rdata() {
    std::shared_lock<std::shared_mutex> lock(detail::string_registry_mutex());
    std::vector<uint8_t> rdata;
    for (const auto& [handle, str] : detail::string_registry()) {
        if (handle == 0 || str.empty()) continue;
        const size_t start = rdata.size();
        const uint32_t len = static_cast<uint32_t>(str.size());
        rdata.resize(start + 8 + len + 1);
        std::memcpy(rdata.data() + start, &handle, 4);
        std::memcpy(rdata.data() + start + 4, &len, 4);
        std::memcpy(rdata.data() + start + 8, str.data(), len);
        rdata[start + 8 + len] = '\0';
        while (rdata.size() % 8 != 0) {
            rdata.push_back(0);
        }
    }
    const size_t end_sentinel = rdata.size();
    rdata.resize(end_sentinel + 8, 0);
    return rdata;
}

} // namespace dgtl::runtime

/* ─── C-LINKAGE EXPORTED RUNTIME BUILTINS & JIT SYMBOLS ─── */

extern "C" {

void __dgtl_rt_register_string(uint32_t handle, const char* data, uint32_t len) noexcept {
    dgtl::runtime::register_dgtl_string(handle, data, len);
}

void __dgtl_rt_unregister_string(uint32_t handle) noexcept {
    dgtl::runtime::unregister_string_handle(handle);
}

const char* __builtin_detect_platform_name() noexcept {
#ifdef _WIN32
    return "windows";
#elif defined(__linux__)
    return "linux";
#elif defined(__APPLE__)
    return "macos";
#else
    return "unknown";
#endif
}

const char* __builtin_detect_architecture() noexcept {
#if defined(__x86_64__) || defined(_M_X64)
    return "x64";
#elif defined(__i386__) || defined(_M_IX86)
    return "x86";
#elif defined(__aarch64__) || defined(_M_ARM64)
    return "arm64";
#elif defined(__arm__) || defined(_M_ARM)
    return "arm";
#else
    return "unknown";
#endif
}

int64_t __builtin_detect_word_size() noexcept {
    return static_cast<int64_t>(sizeof(void*));
}

int64_t __builtin_detect_endianness() noexcept {
    const uint32_t test = 0x12345678;
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&test);
    return (bytes[0] == 0x78) ? 0 : 1; // 0 = Little Endian, 1 = Big Endian
}

int64_t __builtin_get_page_size() noexcept {
#ifdef _WIN32
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    return static_cast<int64_t>(info.dwPageSize);
#else
    return static_cast<int64_t>(sysconf(_SC_PAGESIZE));
#endif
}

int64_t __builtin_get_cpu_count() noexcept {
#ifdef _WIN32
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    return static_cast<int64_t>(info.dwNumberOfProcessors);
#else
    return static_cast<int64_t>(sysconf(_SC_NPROCESSORS_ONLN));
#endif
}

int64_t __builtin_get_total_memory() noexcept {
#ifdef _WIN32
    MEMORYSTATUSEX memory{};
    memory.dwLength = sizeof(memory);
    GlobalMemoryStatusEx(&memory);
    return static_cast<int64_t>(memory.ullTotalPhys);
#else
    return static_cast<int64_t>(sysconf(_SC_PHYS_PAGES)) * __builtin_get_page_size();
#endif
}

int64_t __builtin_detect_simd_width_bits() noexcept {
#if defined(__GNUC__) || defined(__clang__)
    if (__builtin_cpu_supports("avx512f")) return 512;
    if (__builtin_cpu_supports("avx2")) return 256;
    if (__builtin_cpu_supports("avx")) return 256;
    if (__builtin_cpu_supports("sse4.2")) return 128;
#endif

#ifdef _WIN32
    if (IsProcessorFeaturePresent(PF_AVX512F_INSTRUCTIONS_AVAILABLE)) return 512;
    if (IsProcessorFeaturePresent(PF_AVX2_INSTRUCTIONS_AVAILABLE)) return 256;
    if (IsProcessorFeaturePresent(PF_AVX_INSTRUCTIONS_AVAILABLE)) return 256;
#endif

#ifdef __AVX512F__
    return 512;
#elif defined(__AVX2__) || defined(__AVX__)
    return 256;
#elif defined(__ARM_NEON) || defined(__SSE2__)
    return 128;
#else
    return 64;
#endif
}

int64_t __builtin_detect_print_lane_bits() noexcept {
    return __builtin_detect_simd_width_bits();
}

int64_t __builtin_detect_u64_lane_count() noexcept {
    return __builtin_detect_simd_width_bits() / 64;
}

} // extern "C"