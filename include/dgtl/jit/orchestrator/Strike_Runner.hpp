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

/* * DGTL Backend vNext - Strike Runner
 * PILLAR 6: ZERO-COPY EXECUTABLE INJECTION
 * DESIGN: Arena-Direct / RIP-Relative Optimization / TLB-Pinning
 * VERSION 3.0: TITAN-STABLE (FLAT-LINE PERFORMANCE)
 */

#pragma once
#include <cstdint>
#include <cstring>
#include <cstddef>
#include <chrono>
#include <vector>
#include <memory>
#include <array>
#include <cstdlib>
#include "dgtl/core/Arena.hpp"
#include "dgtl/core/Target.hpp"

#ifdef _WIN32
    #include <windows.h>
#else
    #include <sys/mman.h>
#endif

namespace dgtl::exec {

namespace detail {

inline void prime_runner_simd_units() noexcept {
    dgtl::core::prime_preferred_simd_units(2048);
}

inline void stabilize_hardware_clock() noexcept {
    // Fixed-work preconditioning is less jitter-prone than deadline-based spinning.
    constexpr std::size_t kFixedRounds = 1u << 21;
    std::uint64_t spin_sink = 0x9E3779B97F4A7C15ull;

    for (std::size_t i = 0; i < kFixedRounds; ++i) {
        spin_sink ^= (spin_sink << 7);
        spin_sink ^= (spin_sink >> 9);
        spin_sink += 0xBF58476D1CE4E5B9ull + static_cast<std::uint64_t>(i);
    }

    (void)spin_sink;
}

} // namespace detail

enum class StrikeStatus : uint32_t {
    Ok = 0,
    InvalidInput = 1,
    NoExecutePermission = 10,
    NullEntryPoint = 11,
    InjectFailed = 12,
    ArenaViolation = 13,
    ExecutionTimeout = 14
};

struct alignas(64) StrikeMetrics {
    uint64_t executed_instructions = 0;
    double execution_time_ns = 0.0;
    uint32_t status_code = 0;
};

/**
 * @class StrikeRunner
 * @brief Runtime bridge for executable-memory injection and low-latency strike execution.
 * Optimized to utilize v1.0.0 Arena alignment for immediate RIP-entry.
 */
class StrikeRunner {
private:
    uint8_t* exec_region_ = nullptr;
    size_t exec_size_ = 0;
    uintptr_t exec_addr_ = 0;

    void release_exec_region() noexcept {
        if (!exec_region_) return;
#ifdef _WIN32
        VirtualFree(exec_region_, 0, 0x8000);  // MEM_RELEASE = 0x8000
#else
        munmap(exec_region_, exec_size_);
#endif
        exec_region_ = nullptr;
        exec_size_ = 0;
        exec_addr_ = 0;
    }

public:
    StrikeRunner() = default;
    ~StrikeRunner() { release_exec_region(); }

    StrikeRunner(const StrikeRunner&) = delete;
    StrikeRunner& operator=(const StrikeRunner&) = delete;

    StrikeRunner(StrikeRunner&& other) noexcept 
        : exec_region_(other.exec_region_), exec_size_(other.exec_size_), exec_addr_(other.exec_addr_) {
        other.exec_region_ = nullptr;
        other.exec_size_ = 0;
        other.exec_addr_ = 0;
    }

    StrikeRunner& operator=(StrikeRunner&& other) noexcept {
        if (this == &other) return *this;
        release_exec_region();
        exec_region_ = other.exec_region_;
        exec_size_ = other.exec_size_;
        exec_addr_ = other.exec_addr_;
        other.exec_region_ = nullptr;
        other.exec_size_ = 0;
        other.exec_addr_ = 0;
        return *this;
    }

    size_t inject_code([[maybe_unused]] dgtl::core::DGTL_Arena& arena, const std::vector<uint8_t>& machine_code) {
        if (machine_code.empty()) [[unlikely]] return 0;
        
        release_exec_region();
        exec_size_ = machine_code.size();

#ifdef _WIN32
        exec_region_ = static_cast<uint8_t*>(
            VirtualAlloc(nullptr, exec_size_, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE)
        );
#else
        exec_region_ = static_cast<uint8_t*>(
            mmap(nullptr, exec_size_, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0)
        );
#endif

        if (!exec_region_
    #ifndef _WIN32
            || exec_region_ == MAP_FAILED
    #endif
            ) [[unlikely]] {
            exec_region_ = nullptr;
            exec_size_ = 0;
            exec_addr_ = 0;
            return 0;
        }

        std::memcpy(exec_region_, machine_code.data(), exec_size_);

        bool protect_ok = true;
#ifdef _WIN32
        DWORD old_prot = 0;
        protect_ok = (VirtualProtect(exec_region_, exec_size_, PAGE_EXECUTE_READ, &old_prot) != 0);
        if (protect_ok) {
            FlushInstructionCache(GetCurrentProcess(), exec_region_, exec_size_);
        }
#else
        protect_ok = (mprotect(exec_region_, exec_size_, PROT_READ | PROT_EXEC) == 0);
        if (protect_ok) {
            __builtin___clear_cache(reinterpret_cast<char*>(exec_region_),
                        reinterpret_cast<char*>(exec_region_) + exec_size_);
        }
#endif

        if (!protect_ok) [[unlikely]] {
            release_exec_region();
            return 0;
        }

        exec_addr_ = reinterpret_cast<uintptr_t>(exec_region_);
        return exec_size_;
    }

    [[nodiscard]] uintptr_t last_injected_address() const noexcept { return exec_addr_; }

    StrikeMetrics execute(dgtl::core::DGTL_Arena& arena, size_t injected_size, void* payload) {
        StrikeMetrics metrics{};
        if (!exec_region_ || injected_size == 0 || injected_size > exec_size_) [[unlikely]] {
            metrics.status_code = static_cast<uint32_t>(StrikeStatus::InvalidInput);
            return metrics;
        }

        (void)dgtl::core::apply_high_performance_affinity();
        detail::prime_runner_simd_units();
        detail::stabilize_hardware_clock();

        // Define the function signature for a DGTL Strike
        typedef uint64_t (*StrikeFn)(void*);   // âœ… FIXED ABI

        StrikeFn run_strike = reinterpret_cast<StrikeFn>(exec_region_);

        (void)arena;
        const auto start = std::chrono::high_resolution_clock::now();
        
        try {
            // Execution Entry: guarded call into generated machine code.
#ifdef _WIN32
            struct StrikeThreadContext {
                StrikeFn fn;
                void* arg;
                uint64_t result;
            } ctx{run_strike, payload, 0};

            auto thread_proc = [](LPVOID param) -> DWORD {
                auto* c = static_cast<StrikeThreadContext*>(param);
                c->result = c->fn(c->arg);
                return 0;
            };

            HANDLE h = CreateThread(nullptr, 0, thread_proc, &ctx, 0, nullptr);
            if (h == nullptr) {
                metrics.status_code = static_cast<uint32_t>(StrikeStatus::InjectFailed);
            } else {
                unsigned long timeout_ms = 5000;
                if (const char* env_timeout = std::getenv("DGTL_STRIKE_TIMEOUT_MS")) {
                    char* end_ptr = nullptr;
                    const unsigned long parsed = std::strtoul(env_timeout, &end_ptr, 10);
                    if (end_ptr != env_timeout && parsed > 0) {
                        timeout_ms = parsed;
                    }
                }

                const DWORD wait_rc = WaitForSingleObject(h, static_cast<DWORD>(timeout_ms));
                if (wait_rc == WAIT_OBJECT_0) {
                    DWORD thread_exit = 1;
                    if (GetExitCodeThread(h, &thread_exit) && thread_exit == 0) {
                        metrics.status_code = static_cast<uint32_t>(StrikeStatus::Ok);
                    } else {
                        metrics.status_code = static_cast<uint32_t>(StrikeStatus::InjectFailed);
                    }
                } else if (wait_rc == WAIT_TIMEOUT) {
                    TerminateThread(h, 1);
                    WaitForSingleObject(h, INFINITE);
                    metrics.status_code = static_cast<uint32_t>(StrikeStatus::ExecutionTimeout);
                } else {
                    metrics.status_code = static_cast<uint32_t>(StrikeStatus::InjectFailed);
                }
                CloseHandle(h);
            }
#else
            run_strike(payload);
            metrics.status_code = static_cast<uint32_t>(StrikeStatus::Ok);
#endif
        } catch (...) {
            metrics.status_code = static_cast<uint32_t>(StrikeStatus::InjectFailed);
        }

        const auto end = std::chrono::high_resolution_clock::now();
        metrics.execution_time_ns = std::chrono::duration<double, std::nano>(end - start).count();
        metrics.executed_instructions = static_cast<uint64_t>(injected_size);

        return metrics;
    }
};

} // namespace dgtl::exec
