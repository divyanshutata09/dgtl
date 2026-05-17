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
#include <string>
#include <string_view>
#include <iostream>
#include <mutex>
#include <atomic>
#include <unordered_map>
#include "dgtl/utils/container/Flat_Map.hpp"

#ifdef _WIN32
    #include <windows.h>
#else
    #include <unistd.h>
#endif

namespace dgtl::runtime {

// --- Windows VT100 Terminal Color Support ---
namespace {
    inline void enable_vt_console() {
#ifdef _WIN32
        static std::once_flag vt_once;
        std::call_once(vt_once, []() {
            HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
            if (h != INVALID_HANDLE_VALUE) {
                DWORD mode = 0;
                if (GetConsoleMode(h, &mode)) {
                    SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
                }
            }
        });
#endif
    }
}

// Runtime registry owns string storage so payload parsing buffers can be temporary.
// C++17 inline variable: exactly one definition shared across all translation units.
inline dgtl::utils::FastFlatMap<uint32_t, std::string, 4096> g_string_registry;

// Called by the Binary_Loader to populate the registry from the payload.
inline void register_myzon_string(uint32_t handle, const char* data, uint32_t len) {
    (void)g_string_registry.insert(handle, std::string(data, static_cast<std::size_t>(len)));
}

[[nodiscard]] inline std::string_view lookup_string(uint32_t handle) noexcept {
    if (const auto* s = g_string_registry.find(handle)) [[likely]] {
        return *s;
    }
    return "<invalid_fmt_handle>";
}

[[nodiscard]] inline std::string_view try_lookup_string(uint64_t raw_value) noexcept {
    if (raw_value > static_cast<uint64_t>(UINT32_MAX)) {
        return {};
    }
    const auto* s = g_string_registry.find(static_cast<uint32_t>(raw_value));
    if (!s) {
        return {};
    }
    return *s;
}

struct PendingFormattedLineState {
    size_t next_placeholder_index = 0;
    std::string buffer;
};

// Header-local TLS state avoids ODR/linker collisions across translation units.
static thread_local std::unordered_map<uint64_t, PendingFormattedLineState> g_pending_formatted_prints;

[[nodiscard]] inline size_t count_placeholders(std::string_view fmt) noexcept {
    size_t count = 0;
    size_t pos = 0;
    while ((pos = fmt.find("{}", pos)) != std::string_view::npos) {
        ++count;
        pos += 2;
    }
    return count;
}

[[nodiscard]] inline size_t nth_placeholder_pos(std::string_view fmt, size_t index) noexcept {
    size_t pos = 0;
    for (size_t i = 0; i <= index; ++i) {
        pos = fmt.find("{}", pos);
        if (pos == std::string_view::npos) {
            return pos;
        }
        if (i == index) {
            return pos;
        }
        pos += 2;
    }
    return std::string_view::npos;
}

inline void emit_log(std::string_view level, std::string_view text) {
    enable_vt_console();
    std::cerr << "\033[36m[" << level << "]\033[0m " << text << std::endl;
}

// ── MYZON FFI Runtime Hooks ─────────────────────────────────────────────────
// Using std::endl to force immediate buffer flushes. This prevents output
// from getting swallowed if the StrikeOrchestrator exits rapidly.

extern "C" inline void __myzon_runtime_print_str(const char* str) {
    enable_vt_console();
    const auto raw = reinterpret_cast<uintptr_t>(str);
    if (raw <= static_cast<uintptr_t>(UINT32_MAX)) {
        const std::string_view mapped = try_lookup_string(static_cast<uint64_t>(raw));
        if (!mapped.empty()) {
            std::cout << "\033[32m" << mapped << "\033[0m" << std::endl;
            return;
        }
    }
    if (!str) {
        std::cout << "\033[32m(null)\033[0m" << std::endl;
        return;
    }
    std::cout << "\033[32m" << str << "\033[0m" << std::endl;
}

extern "C" inline void __myzon_runtime_print_i64(int64_t value) {
    enable_vt_console();
    std::cout << "\033[32m" << value << "\033[0m" << std::endl;
}

extern "C" inline void __myzon_runtime_print_f64(double value) {
    enable_vt_console();
    std::cout << "\033[32m" << value << "\033[0m" << std::endl;
}

extern "C" inline void __myzon_runtime_print_f32(float value) {
    enable_vt_console();
    std::cout << "\033[32m" << value << "\033[0m" << std::endl;
}

extern "C" inline void __myzon_runtime_print_fmt_str(uint64_t format_handle, int64_t value) {
    enable_vt_console();
    const std::string_view fmt = lookup_string(static_cast<uint32_t>(format_handle));
    const size_t placeholder_count = count_placeholders(fmt);
    if (placeholder_count == 0) {
        std::cout << "\033[32m" << fmt << ' ' << value << "\033[0m" << std::endl;
        return;
    }

    if (placeholder_count == 1) {
        const size_t brace_pos = fmt.find("{}");
        std::cout << "\033[32m"
                  << fmt.substr(0, brace_pos)
                  << value
                  << fmt.substr(brace_pos + 2)
                  << "\033[0m" << std::endl;
        return;
    }

    auto& state = g_pending_formatted_prints[format_handle];
    if (state.next_placeholder_index >= placeholder_count) {
        state.next_placeholder_index = 0;
        state.buffer.clear();
    }

    const size_t current_index = state.next_placeholder_index;
    const size_t current_pos = nth_placeholder_pos(fmt, current_index);
    if (current_pos == std::string_view::npos) {
        g_pending_formatted_prints.erase(format_handle);
        std::cout << "\033[32m" << fmt << ' ' << value << "\033[0m" << std::endl;
        return;
    }

    if (current_index == 0) {
        state.buffer.clear();
        state.buffer.append(fmt.substr(0, current_pos));
    }

    state.buffer.append(std::to_string(value));
    state.next_placeholder_index = current_index + 1;

    if (state.next_placeholder_index < placeholder_count) {
        const size_t next_pos = nth_placeholder_pos(fmt, state.next_placeholder_index);
        if (next_pos == std::string_view::npos || next_pos < (current_pos + 2)) {
            g_pending_formatted_prints.erase(format_handle);
            std::cout << "\033[32m" << fmt << ' ' << value << "\033[0m" << std::endl;
            return;
        }
        state.buffer.append(fmt.substr(current_pos + 2, next_pos - (current_pos + 2)));
        return;
    }

    state.buffer.append(fmt.substr(current_pos + 2));
    std::cout << "\033[32m" << state.buffer << "\033[0m" << std::endl;
    g_pending_formatted_prints.erase(format_handle);
}

extern "C" inline void __myzon_runtime_print_i64x8(int64_t v0, int64_t v1, int64_t v2, int64_t v3,
                                                    int64_t v4, int64_t v5, int64_t v6, int64_t v7) {
    enable_vt_console();
    std::cout << "\033[32m" << v0 << ' ' << v1 << ' ' << v2 << ' ' << v3
              << ' ' << v4 << ' ' << v5 << ' ' << v6 << ' ' << v7 << "\033[0m" << std::endl;
}

// ════════════════════════════════════════════════════════════════════════════
// 512-BIT VECTOR PRINT SYSTEM (AUTO-ADAPTIVE FORMATTING)
// ════════════════════════════════════════════════════════════════════════════

extern "C" inline void __myzon_runtime_print_i64_auto(int64_t value) {
    __myzon_runtime_print_i64(value);
}

extern "C" inline void __myzon_runtime_print_i32(int32_t value) {
    enable_vt_console();
    std::cout << "\033[32m" << value << "\033[0m" << std::endl;
}

extern "C" inline void __myzon_runtime_print_fmt_str_i64x3(uint64_t format_handle, int64_t v0, int64_t v1, int64_t v2) {
    enable_vt_console();
    const std::string_view fmt = lookup_string(static_cast<uint32_t>(format_handle));
    const size_t first = fmt.find("{}");
    if (first == std::string_view::npos) {
        // No placeholders
        std::cout << "\033[32m" << fmt << ' ' << v0 << ' ' << v1 << ' ' << v2 << "\033[0m" << std::endl;
        return;
    }
    const size_t second = fmt.find("{}", first + 2);
    if (second == std::string_view::npos) {
        // Only one placeholder
        std::cout << "\033[32m" << fmt.substr(0, first) << v0
                  << fmt.substr(first + 2) << ' ' << v1 << ' ' << v2 << "\033[0m" << std::endl;
        return;
    }
    const size_t third = fmt.find("{}", second + 2);
    if (third == std::string_view::npos) {
        // Two placeholders
        std::cout << "\033[32m" << fmt.substr(0, first) << v0
                  << fmt.substr(first + 2, second - (first + 2)) << v1
                  << fmt.substr(second + 2) << ' ' << v2 << "\033[0m" << std::endl;
        return;
    }
    // Three or more placeholders: fill first three
    std::cout << "\033[32m" << fmt.substr(0, first) << v0
              << fmt.substr(first + 2, second - (first + 2)) << v1
              << fmt.substr(second + 2, third - (second + 2)) << v2
              << fmt.substr(third + 2) << "\033[0m" << std::endl;
}

extern "C" inline void __myzon_runtime_print_fmt_str_i64x2(uint64_t format_handle, int64_t v0, int64_t v1) {
    enable_vt_console();
    const std::string_view fmt = lookup_string(static_cast<uint32_t>(format_handle));
    const size_t first = fmt.find("{}");
    if (first == std::string_view::npos) {
        // No placeholders
        std::cout << "\033[32m" << fmt << ' ' << v0 << ' ' << v1 << "\033[0m" << std::endl;
        return;
    }
    const size_t second = fmt.find("{}", first + 2);
    if (second == std::string_view::npos) {
        // Only one placeholder
        std::cout << "\033[32m" << fmt.substr(0, first) << v0
                  << fmt.substr(first + 2) << ' ' << v1 << "\033[0m" << std::endl;
        return;
    }
    // Two or more placeholders: fill first two
    std::cout << "\033[32m" << fmt.substr(0, first) << v0
              << fmt.substr(first + 2, second - (first + 2)) << v1
              << fmt.substr(second + 2) << "\033[0m" << std::endl;
}

extern "C" inline void __myzon_runtime_print_fmt_str_i64x4(uint64_t format_handle,
                                                             int64_t v0,
                                                             int64_t v1,
                                                             int64_t v2,
                                                             int64_t v3) {
    enable_vt_console();
    const std::string_view fmt = lookup_string(static_cast<uint32_t>(format_handle));
    const size_t first = fmt.find("{}");
    if (first == std::string_view::npos) {
        std::cout << "\033[32m" << fmt << ' ' << v0 << ' ' << v1 << ' ' << v2 << ' ' << v3 << "\033[0m" << std::endl;
        return;
    }
    const size_t second = fmt.find("{}", first + 2);
    if (second == std::string_view::npos) {
        std::cout << "\033[32m" << fmt.substr(0, first) << v0
                  << fmt.substr(first + 2) << ' ' << v1 << ' ' << v2 << ' ' << v3 << "\033[0m" << std::endl;
        return;
    }
    const size_t third = fmt.find("{}", second + 2);
    if (third == std::string_view::npos) {
        std::cout << "\033[32m" << fmt.substr(0, first) << v0
                  << fmt.substr(first + 2, second - (first + 2)) << v1
                  << fmt.substr(second + 2) << ' ' << v2 << ' ' << v3 << "\033[0m" << std::endl;
        return;
    }
    const size_t fourth = fmt.find("{}", third + 2);
    if (fourth == std::string_view::npos) {
        std::cout << "\033[32m" << fmt.substr(0, first) << v0
                  << fmt.substr(first + 2, second - (first + 2)) << v1
                  << fmt.substr(second + 2, third - (second + 2)) << v2
                  << fmt.substr(third + 2) << ' ' << v3 << "\033[0m" << std::endl;
        return;
    }
    std::cout << "\033[32m" << fmt.substr(0, first) << v0
              << fmt.substr(first + 2, second - (first + 2)) << v1
              << fmt.substr(second + 2, third - (second + 2)) << v2
              << fmt.substr(third + 2, fourth - (third + 2)) << v3
              << fmt.substr(fourth + 2) << "\033[0m" << std::endl;
}

extern "C" inline void __myzon_runtime_print_fmt_str_i64x5(uint64_t format_handle,
                                                             int64_t v0,
                                                             int64_t v1,
                                                             int64_t v2,
                                                             int64_t v3,
                                                             int64_t v4) {
    enable_vt_console();
    const std::string_view fmt = lookup_string(static_cast<uint32_t>(format_handle));
    const size_t pos0 = fmt.find("{}");
    const size_t pos1 = (pos0 != std::string_view::npos) ? fmt.find("{}", pos0 + 2) : std::string_view::npos;
    const size_t pos2 = (pos1 != std::string_view::npos) ? fmt.find("{}", pos1 + 2) : std::string_view::npos;
    const size_t pos3 = (pos2 != std::string_view::npos) ? fmt.find("{}", pos2 + 2) : std::string_view::npos;
    const size_t pos4 = (pos3 != std::string_view::npos) ? fmt.find("{}", pos3 + 2) : std::string_view::npos;
    
    std::string buffer;
    buffer.append(fmt.substr(0, pos0 != std::string_view::npos ? pos0 : fmt.size()));
    if (pos0 != std::string_view::npos) {
        buffer.append(std::to_string(v0));
        buffer.append(fmt.substr(pos0 + 2, (pos1 != std::string_view::npos ? pos1 : fmt.size()) - (pos0 + 2)));
    }
    if (pos1 != std::string_view::npos) {
        buffer.append(std::to_string(v1));
        buffer.append(fmt.substr(pos1 + 2, (pos2 != std::string_view::npos ? pos2 : fmt.size()) - (pos1 + 2)));
    }
    if (pos2 != std::string_view::npos) {
        buffer.append(std::to_string(v2));
        buffer.append(fmt.substr(pos2 + 2, (pos3 != std::string_view::npos ? pos3 : fmt.size()) - (pos2 + 2)));
    }
    if (pos3 != std::string_view::npos) {
        buffer.append(std::to_string(v3));
        buffer.append(fmt.substr(pos3 + 2, (pos4 != std::string_view::npos ? pos4 : fmt.size()) - (pos3 + 2)));
    }
    if (pos4 != std::string_view::npos) {
        buffer.append(std::to_string(v4));
        buffer.append(fmt.substr(pos4 + 2));
    }
    std::cout << "\033[32m" << buffer << "\033[0m" << std::endl;
}

extern "C" inline void __myzon_runtime_print_fmt_str_i64x6(uint64_t format_handle,
                                                             int64_t v0,
                                                             int64_t v1,
                                                             int64_t v2,
                                                             int64_t v3,
                                                             int64_t v4,
                                                             int64_t v5) {
    enable_vt_console();
    const std::string_view fmt = lookup_string(static_cast<uint32_t>(format_handle));
    std::string buffer;
    std::array<int64_t, 6> vals = {v0, v1, v2, v3, v4, v5};
    size_t last_pos = 0;
    for (size_t i = 0; i < 6; ++i) {
        size_t pos = fmt.find("{}", last_pos);
        if (pos == std::string_view::npos) {
            buffer.append(fmt.substr(last_pos));
            break;
        }
        buffer.append(fmt.substr(last_pos, pos - last_pos));
        buffer.append(std::to_string(vals[i]));
        last_pos = pos + 2;
    }
    if (last_pos < fmt.size()) {
        buffer.append(fmt.substr(last_pos));
    }
    std::cout << "\033[32m" << buffer << "\033[0m" << std::endl;
}

extern "C" inline void __myzon_runtime_print_fmt_str_i64x7(uint64_t format_handle,
                                                             int64_t v0,
                                                             int64_t v1,
                                                             int64_t v2,
                                                             int64_t v3,
                                                             int64_t v4,
                                                             int64_t v5,
                                                             int64_t v6) {
    enable_vt_console();
    const std::string_view fmt = lookup_string(static_cast<uint32_t>(format_handle));
    std::string buffer;
    std::array<int64_t, 7> vals = {v0, v1, v2, v3, v4, v5, v6};
    size_t last_pos = 0;
    for (size_t i = 0; i < 7; ++i) {
        size_t pos = fmt.find("{}", last_pos);
        if (pos == std::string_view::npos) {
            buffer.append(fmt.substr(last_pos));
            break;
        }
        buffer.append(fmt.substr(last_pos, pos - last_pos));
        buffer.append(std::to_string(vals[i]));
        last_pos = pos + 2;
    }
    if (last_pos < fmt.size()) {
        buffer.append(fmt.substr(last_pos));
    }
    std::cout << "\033[32m" << buffer << "\033[0m" << std::endl;
}

extern "C" inline void __myzon_runtime_print_fmt_str_i64x8(uint64_t format_handle,
                                                             int64_t v0,
                                                             int64_t v1,
                                                             int64_t v2,
                                                             int64_t v3,
                                                             int64_t v4,
                                                             int64_t v5,
                                                             int64_t v6,
                                                             int64_t v7) {
    enable_vt_console();
    const std::string_view fmt = lookup_string(static_cast<uint32_t>(format_handle));
    std::string buffer;
    std::array<int64_t, 8> vals = {v0, v1, v2, v3, v4, v5, v6, v7};
    size_t last_pos = 0;
    for (size_t i = 0; i < 8; ++i) {
        size_t pos = fmt.find("{}", last_pos);
        if (pos == std::string_view::npos) {
            buffer.append(fmt.substr(last_pos));
            break;
        }
        buffer.append(fmt.substr(last_pos, pos - last_pos));
        buffer.append(std::to_string(vals[i]));
        last_pos = pos + 2;
    }
    if (last_pos < fmt.size()) {
        buffer.append(fmt.substr(last_pos));
    }
    std::cout << "\033[32m" << buffer << "\033[0m" << std::endl;
}

extern "C" inline void __myzon_runtime_print_i64x2(int64_t v0, int64_t v1) {
    enable_vt_console();
    std::cout << "\033[32m" << v0 << ' ' << v1 << "\033[0m" << std::endl;
}

extern "C" inline void __myzon_runtime_print_i64x4(int64_t v0, int64_t v1, int64_t v2, int64_t v3) {
    enable_vt_console();
    std::cout << "\033[32m" << v0 << ' ' << v1 << ' ' << v2 << ' ' << v3 << "\033[0m" << std::endl;
}

extern "C" inline void __myzon_runtime_flush() {
    std::cout.flush();
    std::cerr.flush();
}

// ════════════════════════════════════════════════════════════════════════════
// PLATFORM DETECTION SYSTEM (10 FUNCTIONS)
// ════════════════════════════════════════════════════════════════════════════

extern "C" inline const char* __builtin_detect_platform_name() {
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

extern "C" inline const char* __builtin_detect_architecture() {
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

extern "C" inline int64_t __builtin_detect_word_size() {
    return static_cast<int64_t>(sizeof(void*));
}

extern "C" inline int64_t __builtin_detect_endianness() {
    const uint32_t test = 0x12345678;
    const uint8_t* p = reinterpret_cast<const uint8_t*>(&test);
    return (p[0] == 0x78) ? 0 : 1;
}

extern "C" inline int64_t __builtin_get_page_size() {
    #ifdef _WIN32
        SYSTEM_INFO si;
        GetSystemInfo(&si);
        return static_cast<int64_t>(si.dwPageSize);
    #else
        return static_cast<int64_t>(sysconf(_SC_PAGESIZE));
    #endif
}

extern "C" inline int64_t __builtin_get_cpu_count() {
    #ifdef _WIN32
        SYSTEM_INFO si;
        GetSystemInfo(&si);
        return static_cast<int64_t>(si.dwNumberOfProcessors);
    #else
        return static_cast<int64_t>(sysconf(_SC_NPROCESSORS_ONLN));
    #endif
}

extern "C" inline int64_t __builtin_get_total_memory() {
    #ifdef _WIN32
        MEMORYSTATUSEX mem;
        mem.dwLength = sizeof(mem);
        GlobalMemoryStatusEx(&mem);
        return static_cast<int64_t>(mem.ullTotalPhys);
    #else
        return static_cast<int64_t>(sysconf(_SC_PHYS_PAGES)) * __builtin_get_page_size();
    #endif
}

extern "C" inline int64_t __builtin_detect_simd_width_bits() {
    // Detect AVX-512 capability (512 bits)
    #ifdef __AVX512F__
        return 512;
    #elif defined(__AVX2__)
        return 256;
    #elif defined(__AVX__) || defined(__SSE4_2__)
        return 128;
    #else
        return 64;
    #endif
}

extern "C" inline int64_t __builtin_detect_print_lane_bits() {
    return __builtin_detect_simd_width_bits();
}

extern "C" inline int64_t __builtin_detect_u64_lane_count() {
    return __builtin_detect_simd_width_bits() / 64;
}

extern "C" inline void __myzon_log_info(const char* message) {
    if (!message) {
        emit_log("INFO", "(null)");
        return;
    }
    const auto raw = reinterpret_cast<uintptr_t>(message);
    if (raw <= static_cast<uintptr_t>(UINT32_MAX)) {
        const std::string_view mapped = try_lookup_string(static_cast<uint64_t>(raw));
        if (!mapped.empty()) {
            emit_log("INFO", mapped);
            return;
        }
    }
    emit_log("INFO", std::string_view(message));
}

extern "C" inline void __myzon_log_warn(const char* message) {
    if (!message) {
        emit_log("WARN", "(null)");
        return;
    }
    const auto raw = reinterpret_cast<uintptr_t>(message);
    if (raw <= static_cast<uintptr_t>(UINT32_MAX)) {
        const std::string_view mapped = try_lookup_string(static_cast<uint64_t>(raw));
        if (!mapped.empty()) {
            emit_log("WARN", mapped);
            return;
        }
    }
    emit_log("WARN", std::string_view(message));
}

extern "C" inline void __myzon_log_error(const char* message) {
    if (!message) {
        emit_log("ERROR", "(null)");
        return;
    }
    const auto raw = reinterpret_cast<uintptr_t>(message);
    if (raw <= static_cast<uintptr_t>(UINT32_MAX)) {
        const std::string_view mapped = try_lookup_string(static_cast<uint64_t>(raw));
        if (!mapped.empty()) {
            emit_log("ERROR", mapped);
            return;
        }
    }
    emit_log("ERROR", std::string_view(message));
}

extern "C" inline void __myzon_log_debug(const char* message) {
    if (!message) {
        emit_log("DEBUG", "(null)");
        return;
    }
    const auto raw = reinterpret_cast<uintptr_t>(message);
    if (raw <= static_cast<uintptr_t>(UINT32_MAX)) {
        const std::string_view mapped = try_lookup_string(static_cast<uint64_t>(raw));
        if (!mapped.empty()) {
            emit_log("DEBUG", mapped);
            return;
        }
    }
    emit_log("DEBUG", std::string_view(message));
}

extern "C" inline void __myzon_log_flush() {
    std::cerr.flush();
    std::cout.flush();
}

inline std::atomic<uint64_t> g_telemetry_event_count{0};

extern "C" inline void __myzon_trace_fn_entry(void*, uint64_t) {
    g_telemetry_event_count.fetch_add(1, std::memory_order_relaxed);
}

extern "C" inline void __myzon_trace_fn_exit(void*, uint64_t) {
    g_telemetry_event_count.fetch_add(1, std::memory_order_relaxed);
}

extern "C" inline void __myzon_trace_loop_iter(uint32_t, uint32_t) {
    g_telemetry_event_count.fetch_add(1, std::memory_order_relaxed);
}

extern "C" inline void __myzon_trace_alloc(void*, uint64_t) {
    g_telemetry_event_count.fetch_add(1, std::memory_order_relaxed);
}

extern "C" inline void __myzon_trace_free(void*) {
    g_telemetry_event_count.fetch_add(1, std::memory_order_relaxed);
}

extern "C" inline void __myzon_trace_user_event(uint32_t, const uint8_t*, uint8_t) {
    g_telemetry_event_count.fetch_add(1, std::memory_order_relaxed);
}

extern "C" inline void __myzon_trace_checkpoint(uint32_t) {
    g_telemetry_event_count.fetch_add(1, std::memory_order_relaxed);
}

} // namespace dgtl::runtime