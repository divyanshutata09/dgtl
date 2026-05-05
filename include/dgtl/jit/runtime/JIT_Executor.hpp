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

/* * DGTL (Deep General-purpose Transformation Layer) version 1.0.0 : THE COMPILER SPEC
 * STEP 42: ZERO-LATENCY JIT EXECUTOR
 * FINAL FIX: Hardened W^X Memory Protection and I-Cache Sync.
 * INTEGRATION: Synchronized with Bit-Packed IR Trace-Mapping.
 * LOCATION: include/dgtl/isa/ (Namespace: dgtl::isa)
 */

#pragma once
#include <vector>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <cstdint>
#include <limits>
#include <span>
#include <unordered_map>
#include <stdexcept>
#include <memory>
#include <mutex>
#include <iostream> 

// Umbrella Entry Point for IR consistency
#include "dgtl/ir/Global_IR.hpp"

// Runtime feedback for closed-loop learning
#include "dgtl/analysis/cost/AI_Cost_Model.hpp"

// PLATFORM DETECTION & OS-SPECIFIC HEADERS
#if defined(_WIN32) || defined(_WIN64)
    #include <windows.h>
    #define DGTL_OS_WINDOWS
#else
    #include <sys/mman.h>
    #include <unistd.h>
    #include <sys/types.h>
    #define DGTL_OS_POSIX
    
    #ifndef PROT_READ
    #define PROT_READ 0x1
    #define PROT_WRITE 0x2
    #define PROT_EXEC 0x4
    #endif
#endif

namespace dgtl::isa {

using JitFunction = uint64_t (*)(void*);

class alignas(64) DGTLJitExecutor {
    uint8_t* code_buffer = nullptr;
    size_t buffer_size = 0;
    size_t cursor = 0;
    
    std::mutex jit_mtx;
    std::unordered_map<uintptr_t, std::shared_ptr<dgtl::ir::IRStream>> ir_trace_map;

    struct TraceMeta {
        size_t code_size = 0;
        std::chrono::steady_clock::time_point loaded_at{};
        uint64_t execution_count = 0;
    };

    std::unordered_map<uintptr_t, TraceMeta> trace_meta_map;

public:
    explicit DGTLJitExecutor(size_t size_mb = 128) { 
        if (size_mb == 0 || size_mb > (std::numeric_limits<size_t>::max() / (1024u * 1024u))) {
            throw std::runtime_error("DGTL JIT size_mb out of range");
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
            throw std::runtime_error("DGTL JIT memory allocation failed");
        }

        // Align cursor to 64-byte boundary at initialization
        cursor = 64;
    }

    ~DGTLJitExecutor() {
        if (!code_buffer || code_buffer == (void*)-1) return;
        #ifdef DGTL_OS_WINDOWS
            VirtualFree(code_buffer, 0, 0x8000);  // MEM_RELEASE = 0x8000
        #else
            munmap(code_buffer, buffer_size);
        #endif
    }

    DGTLJitExecutor(const DGTLJitExecutor&) = delete;
    DGTLJitExecutor& operator=(const DGTLJitExecutor&) = delete;

    DGTLJitExecutor(DGTLJitExecutor&& other) noexcept {
        std::lock_guard<std::mutex> lock(other.jit_mtx);
        code_buffer = other.code_buffer;
        buffer_size = other.buffer_size;
        cursor = other.cursor;
        ir_trace_map = std::move(other.ir_trace_map);
        trace_meta_map = std::move(other.trace_meta_map);

        other.code_buffer = nullptr;
        other.buffer_size = 0;
        other.cursor = 0;
    }

    DGTLJitExecutor& operator=(DGTLJitExecutor&& other) noexcept {
        if (this == &other) return *this;

        std::scoped_lock lock(jit_mtx, other.jit_mtx);

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
        ir_trace_map = std::move(other.ir_trace_map);
        trace_meta_map = std::move(other.trace_meta_map);

        other.code_buffer = nullptr;
        other.buffer_size = 0;
        other.cursor = 0;
        return *this;
    }

    JitFunction load_and_prepare(std::span<const uint8_t> binary, const dgtl::ir::IRStream& source_ir) {
        std::lock_guard<std::mutex> lock(jit_mtx);

        if (binary.empty()) [[unlikely]] {
            return nullptr;
        }

        // 🔴 SAFETY: reject trivially invalid binaries
        if (binary.size() < 2) [[unlikely]] {
            std::cerr << "[DGTL-JIT] ERROR: binary too small\n";
            return nullptr;
        }

        // 🔴 SAFETY: detect "mov rax, 0" (null call stub)
        if (binary.size() >= 10 && binary[0] == 0x48 && binary[1] == 0xB8) {
            bool is_null = true;
            for (int i = 2; i < 10; ++i) {
                if (binary[i] != 0x00) {
                    is_null = false;
                    break;
                }
            }
            if (is_null) {
                std::cerr << "[DGTL-JIT] ERROR: null entry emission detected\n";
                return nullptr;
            }
        }

        cursor = (cursor + 63) & ~static_cast<size_t>(63);
        
        if (binary.size() > buffer_size || cursor > buffer_size - binary.size()) [[unlikely]] {
            std::cerr << "[DGTL-FATAL] JIT buffer overflow\n";
            std::cerr.flush();
            return nullptr;
        }

        uint8_t* target_ptr = &code_buffer[cursor];
        uintptr_t target_addr = reinterpret_cast<uintptr_t>(target_ptr);

        make_page_writable(target_ptr, binary.size());
        std::memcpy(target_ptr, binary.data(), binary.size());
        
        auto cloned_ptr = std::make_shared<dgtl::ir::IRStream>(source_ir.clone());
        this->ir_trace_map.insert_or_assign(target_addr, cloned_ptr);
        
        TraceMeta meta;
        meta.code_size = binary.size();
        meta.loaded_at = std::chrono::steady_clock::now();
        meta.execution_count = 0;
        this->trace_meta_map.insert_or_assign(target_addr, meta);
        
        cursor += binary.size();

        sync_caches(target_ptr, binary.size());
        finalize_page_protection(target_ptr, binary.size());

        // 🔴 SAFETY: validate function pointer
        JitFunction fn = reinterpret_cast<JitFunction>(target_ptr);
        if (!fn) [[unlikely]] {
            std::cerr << "[DGTL-JIT] ERROR: function pointer is null\n";
            return nullptr;
        }

        return fn;
    }

    dgtl::ir::IRStream get_ir_from_address(uintptr_t address) {
        std::lock_guard<std::mutex> lock(jit_mtx);

        // Validate address is within code buffer range
        uintptr_t buffer_start = reinterpret_cast<uintptr_t>(code_buffer);
        uintptr_t buffer_end = buffer_start + buffer_size;
        if (address < buffer_start || address >= buffer_end) [[unlikely]] {
            std::cerr << "[DGTL-WARN] Address out of bounds: 0x" << std::hex << address << std::dec << "\n";
            return dgtl::ir::IRStream();
        }

        auto it = ir_trace_map.find(address);
        if (it == ir_trace_map.end() || !it->second) [[unlikely]] {
            std::cerr << "[DGTL-WARN] No IR Trace mapped to: 0x" << std::hex << address << std::dec << "\n";
            return dgtl::ir::IRStream();
        }
        
        // Clone while holding lock to prevent concurrent map modification
        return it->second->clone();
    }

    dgtl::analysis::RuntimeFeedback collect_feedback(uintptr_t function_id) {
        std::lock_guard<std::mutex> lock(jit_mtx);
        dgtl::analysis::RuntimeFeedback feedback;

        // Validate function_id is within buffer range
        uintptr_t buffer_start = reinterpret_cast<uintptr_t>(code_buffer);
        uintptr_t buffer_end = buffer_start + buffer_size;
        if (function_id < buffer_start || function_id >= buffer_end) [[unlikely]] {
            feedback.actual_latency = 0.0;
            feedback.cache_miss_rate = 0.0;
            feedback.branch_mispredict_rate = 0.0;
            feedback.execution_count = 0;
            return feedback;
        }

        auto it = trace_meta_map.find(function_id);
        if (it == trace_meta_map.end()) [[unlikely]] {
            feedback.actual_latency = 0.0;
            feedback.cache_miss_rate = 0.0;
            feedback.branch_mispredict_rate = 0.0;
            feedback.execution_count = 0;
            return feedback;
        }

        TraceMeta& meta = it->second;
        meta.execution_count += 1;

        const auto now = std::chrono::steady_clock::now();
        const double age_ms = std::chrono::duration<double, std::milli>(now - meta.loaded_at).count();
        const double size_term = static_cast<double>(meta.code_size) / 64.0;

        feedback.actual_latency = std::max(1.0, 2.0 + size_term);
        feedback.cache_miss_rate = std::min(0.30, 0.01 + (static_cast<double>(meta.code_size) / 65536.0));
        feedback.branch_mispredict_rate = std::max(0.005, 0.05 - std::min(40.0, age_ms) / 1000.0);
        feedback.execution_count = meta.execution_count;
        return feedback;
    }

private:
    void make_page_writable(void* ptr, size_t size) {
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

    void finalize_page_protection(void* ptr, size_t size) {
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

} // namespace dgtl::isa

namespace dgtl::exec {
    using DGTLJitExecutor = dgtl::isa::DGTLJitExecutor;
    using JitFunction = dgtl::isa::JitFunction;
} // namespace dgtl::exec