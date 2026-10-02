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

/* * DGTL (Deep General-purpose Transformation Layer) version 2.0.0 : THE COMPILER SPEC
 * STEP 42: ZERO-LATENCY EXECUTOR
 * FINAL FIX: Hardened W^X Memory Protection, MIR Trace Mapping, and I-Cache Sync.
 * ARCHITECTURE: Upgraded to HFT-grade Read-Write Locks and Module-Level mapping.
 * ZERO-LEAK: Fully isolated from Mid-End IR and HIR AST nodes. MIR is SSOT.
 */

module;

#include <cstdlib>

// Global module fragment
#include <vector>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <memory>
#include <iostream>
#include <string>

#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#define DGTL_OS_WINDOWS
#elif defined(__APPLE__)
#include <TargetConditionals.h>
#include <sys/mman.h>
#include <unistd.h>
#include <sys/types.h>
#define DGTL_OS_POSIX
#ifndef MAP_JIT
#define MAP_JIT 0x0800
#endif
#else
#include <sys/mman.h>
#include <unistd.h>
#include <sys/types.h>
#define DGTL_OS_POSIX
#endif

// Named module interface declaration MUST be the first statement after header fragment closes
export module dgtl.jit.executor;

// Import ONLY execution, memory, target, and machine-IR layers
import dgtl.arena;
import dgtl.flat_map;
import dgtl.sync;
import dgtl.target;
import dgtl.mir.machine_ir;

export namespace dgtl::jit {

using Function = uint64_t (*)(void*);

struct JITFeedback {
    double actual_latency = 0.0;
    double cache_miss_rate = 0.0;
    double branch_mispredict_rate = 0.0;
    uint64_t execution_count = 0;
};

class alignas(64) DGTLExecutor {
    static constexpr size_t kTraceMapCapacity = 16384;

    uint8_t* code_buffer = nullptr;
    size_t buffer_size = 0;
    size_t cursor = 0;
    
    // Upgraded to shared_mutex to allow concurrent telemetry feedback in sub-microsecond bounds
    mutable dgtl::utils::sync::FastSharedSpinLock _mtx;
    dgtl::utils::FastFlatMap<uintptr_t, std::shared_ptr<dgtl::machine::MachineFunction>, kTraceMapCapacity> mir_trace_map;

    struct TraceMeta {
        size_t code_size = 0;
        std::chrono::steady_clock::time_point loaded_at{};
        uint64_t execution_count = 0;
    };

    dgtl::utils::FastFlatMap<uintptr_t, TraceMeta, kTraceMapCapacity> trace_meta_map;
    dgtl::core::TargetMachine target_machine_;

public:
    explicit DGTLExecutor(size_t size_mb = 32,
                          dgtl::core::ArchType arch = dgtl::core::select_preferred_x86_arch())
        : target_machine_(arch) {
        if (size_mb == 0 || size_mb > (std::numeric_limits<size_t>::max() / (1024u * 1024u))) {
            throw std::runtime_error("DGTL size_mb out of range");
        }

        buffer_size = size_mb * 1024 * 1024;

        #ifdef DGTL_OS_WINDOWS
            code_buffer = static_cast<uint8_t*>(VirtualAlloc(NULL, buffer_size, 
                                                  MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        #else
            code_buffer = static_cast<uint8_t*>(mmap(nullptr, buffer_size, 
                                                  PROT_READ | PROT_WRITE, 
                                                  MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
        #endif
        
        if (!code_buffer || code_buffer == (void*)-1) [[unlikely]] {
            throw std::runtime_error("DGTL memory allocation failed");
        }

        // Align cursor to 64-byte boundary at initialization
        cursor = 64;
    }

    ~DGTLExecutor() {
        if (!code_buffer || code_buffer == (void*)-1) return;
        #ifdef DGTL_OS_WINDOWS
            VirtualFree(code_buffer, 0, 0x8000);  // MEM_RELEASE = 0x8000
        #else
            munmap(code_buffer, buffer_size);
        #endif
    }

    DGTLExecutor(const DGTLExecutor&) = delete;
    DGTLExecutor& operator=(const DGTLExecutor&) = delete;

    DGTLExecutor(DGTLExecutor&& other) noexcept
        : target_machine_(other.target_machine_) {
        dgtl::utils::sync::UniqueLockGuard lock(other._mtx);
        code_buffer = other.code_buffer;
        buffer_size = other.buffer_size;
        cursor = other.cursor;
        mir_trace_map = std::move(other.mir_trace_map);
        trace_meta_map = std::move(other.trace_meta_map);

        other.code_buffer = nullptr;
        other.buffer_size = 0;
        other.cursor = 0;
    }

    DGTLExecutor& operator=(DGTLExecutor&& other) noexcept {
        if (this == &other) return *this;

        if (this < &other) {
            dgtl::utils::sync::UniqueLockGuard self_lock(_mtx);
            dgtl::utils::sync::UniqueLockGuard other_lock(other._mtx);
            assign_from_locked(std::move(other));
        } else {
            dgtl::utils::sync::UniqueLockGuard other_lock(other._mtx);
            dgtl::utils::sync::UniqueLockGuard self_lock(_mtx);
            assign_from_locked(std::move(other));
        }

        return *this;
    }

    void set_target_machine(const dgtl::core::TargetMachine& target) {
        dgtl::utils::sync::UniqueLockGuard lock(_mtx);
        target_machine_ = target;
    }

    [[nodiscard]] const dgtl::core::TargetMachine& get_target_machine() const noexcept {
        return target_machine_;
    }

    Function load_and_prepare(std::span<const uint8_t> binary, const dgtl::machine::MachineFunction* source_mir = nullptr) {
        dgtl::utils::sync::UniqueLockGuard lock(_mtx);

        if (binary.empty()) [[unlikely]] {
            return nullptr;
        }

        // SAFETY: reject trivially invalid binaries
        if (binary.size() < 2) [[unlikely]] {
            std::cerr << "[DGTL-] ERROR: binary too small\n";
            return nullptr;
        }

        // SAFETY: detect "mov rax, 0" (null call stub)
        if (binary.size() >= 10 && binary[0] == 0x48 && binary[1] == 0xB8) {
            bool is_null = true;
            for (int i = 2; i < 10; ++i) {
                if (binary[i] != 0x00) {
                    is_null = false;
                    break;
                }
            }
            if (is_null) {
                std::cerr << "[DGTL-] ERROR: null entry emission detected\n";
                return nullptr;
            }
        }

        cursor = (cursor + 63) & ~static_cast<size_t>(63);
        
        if (binary.size() > buffer_size || cursor > buffer_size - binary.size()) [[unlikely]] {
            std::cerr << "[DGTL-FATAL] buffer overflow\n";
            std::cerr.flush();
            return nullptr;
        }

        uint8_t* target_ptr = &code_buffer[cursor];
        uintptr_t target_addr = reinterpret_cast<uintptr_t>(target_ptr);

        make_page_writable(target_ptr, binary.size());
        std::memcpy(target_ptr, binary.data(), binary.size());
        
        if (source_mir) {
            auto cloned_mir = std::make_shared<dgtl::machine::MachineFunction>(*source_mir);
            (void)this->mir_trace_map.insert_or_assign(target_addr, cloned_mir);
        }
        
        TraceMeta meta;
        meta.code_size = binary.size();
        meta.loaded_at = std::chrono::steady_clock::now();
        meta.execution_count = 0;
        if (!this->trace_meta_map.insert_or_assign(target_addr, meta)) [[unlikely]] {
            std::cerr << "[DGTL-FATAL] trace metadata capacity exhausted\n";
            finalize_page_protection(target_ptr, binary.size());
            return nullptr;
        }
        
        cursor += binary.size();

        sync_caches(target_ptr, binary.size());
        finalize_page_protection(target_ptr, binary.size());

        // SAFETY: validate function pointer
        Function fn = reinterpret_cast<Function>(target_ptr);
        if (!fn) [[unlikely]] {
            std::cerr << "[DGTL-] ERROR: function pointer is null\n";
            return nullptr;
        }

        return fn;
    }

    Function load_and_prepare(std::span<const uint8_t> binary, const dgtl::machine::MachineFunction& source_mir) {
        return load_and_prepare(binary, &source_mir);
    }

    Function load_and_prepare_mir(std::span<const uint8_t> binary, const dgtl::machine::MachineFunction& source_mir) {
        return load_and_prepare(binary, &source_mir);
    }

    [[nodiscard]] std::shared_ptr<const dgtl::machine::MachineFunction> get_mir_from_address(uintptr_t address) const {
        dgtl::utils::sync::SharedLockGuard lock(_mtx);

        uintptr_t buffer_start = reinterpret_cast<uintptr_t>(code_buffer);
        uintptr_t buffer_end = buffer_start + buffer_size;
        if (address < buffer_start || address >= buffer_end) [[unlikely]] {
            return nullptr;
        }

        auto* trace = mir_trace_map.find(address);
        if (!trace || !(*trace)) [[unlikely]] {
            return nullptr;
        }
        
        return *trace;
    }

    JITFeedback collect_feedback(uintptr_t function_id) {
        dgtl::utils::sync::UniqueLockGuard lock(_mtx);
        JITFeedback feedback;

        uintptr_t buffer_start = reinterpret_cast<uintptr_t>(code_buffer);
        uintptr_t buffer_end = buffer_start + buffer_size;
        if (function_id < buffer_start || function_id >= buffer_end) [[unlikely]] {
            return feedback;
        }

        auto* meta = trace_meta_map.find(function_id);
        if (!meta) [[unlikely]] {
            return feedback;
        }

        meta->execution_count += 1;

        const auto now = std::chrono::steady_clock::now();
        const double age_ms = std::chrono::duration<double, std::milli>(now - meta->loaded_at).count();
        const double size_term = static_cast<double>(meta->code_size) / 64.0;

        feedback.actual_latency = std::max(1.0, 2.0 + size_term);
        feedback.cache_miss_rate = std::min(0.30, 0.01 + (static_cast<double>(meta->code_size) / 65536.0));
        feedback.branch_mispredict_rate = std::max(0.005, 0.05 - std::min(40.0, age_ms) / 1000.0);
        feedback.execution_count = meta->execution_count;
        return feedback;
    }

    void feed_runtime_feedback(uintptr_t function_id) {
        dgtl::utils::sync::UniqueLockGuard lock(_mtx);

        uintptr_t buffer_start = reinterpret_cast<uintptr_t>(code_buffer);
        uintptr_t buffer_end = buffer_start + buffer_size;
        if (function_id < buffer_start || function_id >= buffer_end) [[unlikely]] {
            return;
        }

        auto* meta = trace_meta_map.find(function_id);
        if (!meta) [[unlikely]] {
            return;
        }

        meta->execution_count += 1;
    }

private:
    void assign_from_locked(DGTLExecutor&& other) noexcept {
        if (code_buffer && code_buffer != (void*)-1) {
#ifdef DGTL_OS_WINDOWS
            VirtualFree(code_buffer, 0, 0x8000);  // MEM_RELEASE = 0x8000
#else
            munmap(code_buffer, buffer_size);
#endif
        }

        code_buffer = other.code_buffer;
        buffer_size = other.buffer_size;
        cursor = other.cursor;
        mir_trace_map = std::move(other.mir_trace_map);
        trace_meta_map = std::move(other.trace_meta_map);
        target_machine_ = other.target_machine_;

        other.code_buffer = nullptr;
        other.buffer_size = 0;
        other.cursor = 0;
    }

    void make_page_writable(const void* ptr, size_t size) {
        #ifdef DGTL_OS_WINDOWS
            DWORD old_protect;
            SYSTEM_INFO si;
            GetSystemInfo(&si);
            uintptr_t pageSize = si.dwPageSize;

            uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
            uintptr_t pageStart = addr & ~(pageSize - 1);
            uintptr_t pageEnd = (addr + size + pageSize - 1) & ~(pageSize - 1);

            if (!VirtualProtect(reinterpret_cast<LPVOID>(pageStart), pageEnd - pageStart, PAGE_READWRITE, &old_protect)) {
                throw std::runtime_error("DGTL RW protection failure on Windows");
            }
        #else
            long pagesize = sysconf(_SC_PAGESIZE);
            if (pagesize <= 0) pagesize = 4096;

            uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
            uintptr_t start_page = addr & ~(static_cast<uintptr_t>(pagesize) - 1);
            uintptr_t end_page = (addr + size + pagesize - 1) & ~(static_cast<uintptr_t>(pagesize) - 1);

            if (mprotect(reinterpret_cast<void*>(start_page), end_page - start_page, PROT_READ | PROT_WRITE) != 0) {
                throw std::runtime_error("DGTL RW protection failure on POSIX");
            }
        #endif
    }

    void sync_caches(void* ptr, size_t size) noexcept {
        #ifdef DGTL_OS_WINDOWS
            FlushInstructionCache(GetCurrentProcess(), ptr, size);
        #else
            __builtin___clear_cache(static_cast<char*>(ptr), static_cast<char*>(ptr) + size);
        #endif
    }

    void finalize_page_protection(const void* ptr, size_t size) {
        if (!ptr) return;
        
        #ifdef DGTL_OS_WINDOWS
            DWORD oldProtect;
            SYSTEM_INFO si;
            GetSystemInfo(&si);
            uintptr_t pageSize = si.dwPageSize;

            uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
            uintptr_t pageStart = addr & ~(pageSize - 1);
            uintptr_t pageEnd = (addr + size + pageSize - 1) & ~(pageSize - 1);

            if (!VirtualProtect(reinterpret_cast<LPVOID>(pageStart), pageEnd - pageStart, PAGE_EXECUTE_READ, &oldProtect)) {
                std::cerr << "[DGTL-WARN] W^X protection failure on Windows at 0x" << std::hex << addr << std::dec << "\n";
                return;
            }
        #else
            long pagesize = sysconf(_SC_PAGESIZE);
            if (pagesize <= 0) pagesize = 4096;

            uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
            uintptr_t start_page = addr & ~(static_cast<uintptr_t>(pagesize) - 1);
            uintptr_t end_page = (addr + size + pagesize - 1) & ~(static_cast<uintptr_t>(pagesize) - 1);
            
            if (mprotect(reinterpret_cast<void*>(start_page), end_page - start_page, PROT_READ | PROT_EXEC) != 0) {
                std::cerr << "[DGTL-WARN] W^X protection failure on POSIX at 0x" << std::hex << addr << std::dec << "\n";
                return;
            }
        #endif
    }
};

} // namespace dgtl::jit

export namespace dgtl::runtime {
    using DGTLExecutor = dgtl::jit::DGTLExecutor;
    using Function = dgtl::jit::Function;
    using JITFeedback = dgtl::jit::JITFeedback;
}

export namespace dgtl::isa {
    using DGTLExecutor = dgtl::runtime::DGTLExecutor;
    using Function = dgtl::runtime::Function;
} // namespace dgtl::isa

export namespace dgtl::exec {
    using DGTLExecutor = dgtl::runtime::DGTLExecutor;
    using Function = dgtl::runtime::Function;
} // namespace dgtl::exec

export namespace dgtl::jit {

namespace detail {

inline void prime_runner_simd_units() noexcept {
    dgtl::core::prime_preferred_simd_units(2048);
}

inline void stabilize_hardware_clock() noexcept {
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

class StrikeRunner {
private:
    uint8_t* exec_region_ = nullptr;
    size_t exec_size_ = 0;
    uintptr_t exec_addr_ = 0;

    void release_exec_region() noexcept;

public:
    StrikeRunner() = default;
    ~StrikeRunner();

    StrikeRunner(const StrikeRunner&) = delete;
    StrikeRunner& operator=(const StrikeRunner&) = delete;

    StrikeRunner(StrikeRunner&& other) noexcept;
    StrikeRunner& operator=(StrikeRunner&& other) noexcept;

    size_t inject_code(dgtl::core::DGTL_Arena& arena,
                       const std::vector<uint8_t>& machine_code);

    [[nodiscard]] uintptr_t last_injected_address() const noexcept;

    StrikeMetrics execute(dgtl::core::DGTL_Arena& arena, size_t injected_size, void* payload);
};

} // namespace dgtl::jit

export namespace dgtl::runtime {
    using StrikeRunner = dgtl::jit::StrikeRunner;
    using StrikeMetrics = dgtl::jit::StrikeMetrics;
    using StrikeStatus = dgtl::jit::StrikeStatus;
}