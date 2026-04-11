/* * DGTL-MLIR-X v1.0 : THE COMPILER SPEC
 * STEP 42: ZERO-LATENCY JIT EXECUTOR
 * FINAL FIX: Hardened W^X Memory Protection and I-Cache Sync.
 * INTEGRATION: Synchronized with Bit-Packed IR Trace-Mapping.
 * LOCATION: include/dgtl/isa/ (Namespace: dgtl::isa)
 */

#pragma once
#include <vector>
#include <cstring>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <stdexcept>
#include <memory>
#include <mutex>
#include <iostream> 

// Umbrella Entry Point for IR consistency
#include "../ir/Global_IR.hpp"

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

/**
 * @brief Generic JIT Function signature.
 * Returns uint64_t and takes a void* context (usually the Arena or State).
 */
using JitFunction = uint64_t (*)(void*);

/**
 * @class DGTLJitExecutor
 * @brief Manages the executable memory region for AArch64/x86_64 JIT execution.
 * Implements strict W^X protection and I-Cache synchronization.
 */
class alignas(64) DGTLJitExecutor {
    uint8_t* code_buffer = nullptr;
    size_t buffer_size = 0;
    size_t cursor = 0;
    
    std::mutex jit_mtx;
    // Map: Physical Address -> Cloned IR Trace (for AOT/Optimized Re-JIT)
    std::unordered_map<uintptr_t, std::shared_ptr<dgtl::ir::IRStream>> ir_trace_map;

public:
    explicit DGTLJitExecutor(size_t size_mb = 128) { 
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
            std::cerr << "\033[1;31m[DGTL-FATAL]\033[0m JIT memory allocation failed\n";
            std::cerr.flush();
            throw std::bad_alloc();
        }
    }

    ~DGTLJitExecutor() {
        if (!code_buffer || code_buffer == (void*)-1) return;
        #ifdef DGTL_OS_WINDOWS
            VirtualFree(code_buffer, 0, MEM_RELEASE);
        #else
            munmap(code_buffer, buffer_size);
        #endif
    }

    /**
     * @brief Prepares binary for execution and maps it to the source IR.
     * @return A callable JitFunction pointer.
     */
    JitFunction load_and_prepare(std::span<const uint8_t> binary, const dgtl::ir::IRStream& source_ir) {
        std::lock_guard<std::mutex> lock(jit_mtx);

        // Alignment: 64-byte for hardware cache line efficiency
        cursor = (cursor + 63) & ~static_cast<size_t>(63);
        
        if (binary.size() > buffer_size || cursor > buffer_size - binary.size()) [[unlikely]] {
            std::cerr << "[DGTL-FATAL] JIT buffer overflow\n";
            std::cerr.flush();
            return nullptr;
        }

        uint8_t* target_ptr = &code_buffer[cursor];
        uintptr_t target_addr = reinterpret_cast<uintptr_t>(target_ptr);

        // 1. Transactional Write to RW memory
        std::memcpy(target_ptr, binary.data(), binary.size());
        
        // 2. Synchronize Trace-Map (Cloning ensures IR persistence)
        auto cloned_ptr = std::make_shared<dgtl::ir::IRStream>(source_ir.clone());
        this->ir_trace_map.insert_or_assign(target_addr, cloned_ptr);
        
        cursor += binary.size();

        // 3. I-Cache Sync: Ensure the CPU doesn't execute stale instructions
        sync_caches(target_ptr, binary.size());

        // 4. W^X Transition: Flip memory from Write to Execute
        finalize_page_protection(target_ptr, binary.size());

        return reinterpret_cast<JitFunction>(target_ptr);
    }

    /**
     * @brief Back-resolves a physical jump address to its original BIR stream.
     */
    dgtl::ir::IRStream get_ir_from_address(uint64_t address) {
        std::lock_guard<std::mutex> lock(jit_mtx);
        auto it = ir_trace_map.find(static_cast<uintptr_t>(address));
        
        if (it != ir_trace_map.end() && it->second) {
            return it->second->clone(); 
        }
        
        std::cerr << "[DGTL-WARN] No IR Trace mapped to: 0x" << std::hex << address << std::dec << "\n";
        return dgtl::ir::IRStream();
    }

private:
    void sync_caches(void* ptr, size_t size) noexcept {
        #ifdef DGTL_OS_WINDOWS
            FlushInstructionCache(GetCurrentProcess(), ptr, size);
        #else
            __builtin___clear_cache(static_cast<char*>(ptr), static_cast<char*>(ptr) + size);
        #endif
    }

    void finalize_page_protection(void* ptr, size_t size) {
        #ifdef DGTL_OS_WINDOWS
            DWORD oldProtect;
            SYSTEM_INFO si;
            GetSystemInfo(&si);
            uintptr_t pageSize = si.dwPageSize;

            uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
            uintptr_t pageStart = addr & ~(pageSize - 1);
            uintptr_t pageEnd = (addr + size + pageSize - 1) & ~(pageSize - 1);

            if (!VirtualProtect(reinterpret_cast<LPVOID>(pageStart), pageEnd - pageStart, PAGE_EXECUTE_READ, &oldProtect)) {
                std::cerr << "[DGTL-FATAL] VirtualProtect W^X Failure. Error: " << GetLastError() << "\n";
                throw std::runtime_error("DGTLJit: VirtualProtect Failed");
            }
        #else
            long pagesize = sysconf(_SC_PAGESIZE);
            if (pagesize <= 0) pagesize = 4096;

            uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
            uintptr_t start_page = addr & ~(static_cast<uintptr_t>(pagesize) - 1);
            uintptr_t end_page = (addr + size + pagesize - 1) & ~(static_cast<uintptr_t>(pagesize) - 1);
            
            if (mprotect(reinterpret_cast<void*>(start_page), end_page - start_page, PROT_READ | PROT_EXEC) != 0) {
                throw std::runtime_error("DGTLJit: mprotect Failed during W^X transition");
            }
        #endif
    }
};

} // namespace dgtl::isa

/**
 * @brief Global Alias for DGTL-Zenith high-level logic.
 */
namespace dgtl::exec {
    using DGTLJitExecutor = dgtl::isa::DGTLJitExecutor;
    using JitFunction = dgtl::isa::JitFunction;
}
