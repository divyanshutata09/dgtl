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

// Architecture rule of thumb:
// - dgtl/core/NUMA.ixx owns topology/policy primitives.
// - dgtl/runtime/NUMA_Runtime.ixx is the OS-edge wiring facade and should be
//   imported only in runtime/IR execution edge components.
// - Frontend and generic utility code should not depend on NUMA placement.

module;

#include <cstdlib>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <thread>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <malloc.h>
#  include <windows.h>
#else
#  include <pthread.h>
#  include <sched.h>
#  include <sys/mman.h>
#  include <unistd.h>
#endif

export module dgtl.numa_runtime;

import dgtl.numa;

export namespace dgtl::runtime::numa {

[[nodiscard]] inline uint32_t get_num_nodes() noexcept {
    const dgtl::core::NUMATopology topo = dgtl::core::probe_numa_topology();
    return topo.available() ? static_cast<uint32_t>(topo.node_count) : 1u;
}

[[nodiscard]] inline uint32_t get_current_node() noexcept {
#if defined(_WIN32)
    PROCESSOR_NUMBER proc{};
    GetCurrentProcessorNumberEx(&proc);
    USHORT node = 0;
    if (GetNumaProcessorNodeEx(&proc, &node) != 0) {
        return static_cast<uint32_t>(node);
    }
    return 0u;
#elif defined(__linux__)
    unsigned cpu = 0;
    unsigned node = 0;
    if (::getcpu(&cpu, &node) == 0) {
        return static_cast<uint32_t>(node);
    }
    return 0u;
#else
    return 0u;
#endif
}

[[nodiscard]] inline bool pin_thread_to_core(uint32_t core_index) noexcept {
#if defined(_WIN32)
    GROUP_AFFINITY affinity{};
    affinity.Group = static_cast<WORD>(core_index / 64u);
    affinity.Mask = static_cast<KAFFINITY>(1ull << (core_index % 64u));
    return SetThreadGroupAffinity(GetCurrentThread(), &affinity, nullptr) != 0;
#elif defined(__linux__)
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(static_cast<int>(core_index), &cpuset);
    return pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset) == 0;
#else
    (void)core_index;
    return false;
#endif
}

[[nodiscard]] inline bool pin_current_thread_to_node(uint32_t node) noexcept {
    dgtl::core::NUMAController controller;
    controller.set_policy(dgtl::core::NUMAPolicy::PreferLocal);
    controller.set_preferred_node(static_cast<uint16_t>(node));
    return controller.apply_policy();
}

[[nodiscard]] inline void* allocate_on_node(size_t size, uint32_t node, size_t alignment = 64u) noexcept {
    if (size == 0) {
        return nullptr;
    }

    (void)pin_current_thread_to_node(node);

    // Ensure alignment is at least sizeof(void*) and a power of 2
    size_t actual_alignment = (alignment < sizeof(void*)) ? sizeof(void*) : alignment;
    if ((actual_alignment & (actual_alignment - 1)) != 0) {
        actual_alignment = 64u;
    }

#if defined(_WIN32)
    return _aligned_malloc(size, actual_alignment);
#else
    void* ptr = nullptr;
    if (posix_memalign(&ptr, actual_alignment, size) != 0) {
        return nullptr;
    }
    return ptr;
#endif
}

inline void free_allocation(void* ptr) noexcept {
    if (!ptr) {
        return;
    }
#if defined(_WIN32)
    _aligned_free(ptr);
#else
    std::free(ptr);
#endif
}

inline void free_on_node(void* ptr, [[maybe_unused]] size_t size = 0) noexcept {
    free_allocation(ptr);
}

[[nodiscard]] inline void* allocate_executable_on_node(size_t size, uint32_t node) noexcept {
    if (size == 0) {
        return nullptr;
    }

#if defined(_WIN32)
    void* p = VirtualAllocExNuma(GetCurrentProcess(), nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE,
                                 static_cast<DWORD>(node));
    if (!p) {
        p = VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    }
    return p;
#else
    (void)node;
    void* p = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return (p == MAP_FAILED) ? nullptr : p;
#endif
}

inline void free_executable_region(void* ptr, size_t size) noexcept {
    if (!ptr) {
        return;
    }
#if defined(_WIN32)
    (void)size;
    constexpr DWORD kMemRelease = 0x00008000u; // MEM_RELEASE
    VirtualFree(ptr, 0, kMemRelease);
#else
    munmap(ptr, size);
#endif
}

struct DualMappedRegion {
    void* rw_ptr = nullptr;
    void* rx_ptr = nullptr;
    size_t size = 0;
#if defined(_WIN32)
    void* handle = nullptr;
#else
    int fd = -1;
#endif
};

[[nodiscard]] inline DualMappedRegion allocate_dual_mapped(size_t size) noexcept {
    DualMappedRegion region{};
    if (size == 0) return region;

    size = (size + 65535ULL) & ~65535ULL;
    region.size = size;

#if defined(_WIN32)
    HANDLE mapping = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_EXECUTE_READWRITE, 
                                        static_cast<DWORD>(size >> 32), static_cast<DWORD>(size & 0xFFFFFFFFULL), nullptr);
    if (!mapping) {
        region.rw_ptr = VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        region.rx_ptr = region.rw_ptr;
        return region;
    }
    region.handle = mapping;
    region.rw_ptr = MapViewOfFile(mapping, FILE_MAP_WRITE | FILE_MAP_READ, 0, 0, size);
    region.rx_ptr = MapViewOfFile(mapping, FILE_MAP_READ | FILE_MAP_EXECUTE, 0, 0, size);
    if (!region.rw_ptr || !region.rx_ptr) {
        if (region.rw_ptr) UnmapViewOfFile(region.rw_ptr);
        if (region.rx_ptr && region.rx_ptr != region.rw_ptr) UnmapViewOfFile(region.rx_ptr);
        CloseHandle(mapping);
        region.handle = nullptr;
        region.rw_ptr = VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        region.rx_ptr = region.rw_ptr;
    }
    return region;
#else
    region.rw_ptr = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    region.rx_ptr = region.rw_ptr;
    return region;
#endif
}

inline void free_dual_mapped(DualMappedRegion& region) noexcept {
#if defined(_WIN32)
    if (region.handle) {
        if (region.rw_ptr) UnmapViewOfFile(region.rw_ptr);
        if (region.rx_ptr && region.rx_ptr != region.rw_ptr) UnmapViewOfFile(region.rx_ptr);
        CloseHandle(reinterpret_cast<HANDLE>(region.handle));
        region = {};
        return;
    }
    if (region.rw_ptr) {
        VirtualFree(region.rw_ptr, 0, 0x00008000u);
        region = {};
    }
#else
    if (region.rw_ptr && region.rw_ptr != MAP_FAILED) {
        munmap(region.rw_ptr, region.size);
        region = {};
    }
#endif
}

[[nodiscard]] inline bool make_executable(void* ptr, size_t size) noexcept {
    if (!ptr || size == 0) {
        return false;
    }

#if defined(_WIN32)
    DWORD old_protect = 0;
    if (VirtualProtect(ptr, size, PAGE_EXECUTE_READ, &old_protect) == 0) {
        return false;
    }
    // Flush CPU Instruction Cache to prevent executing stale prefetched instructions
    return FlushInstructionCache(GetCurrentProcess(), ptr, size) != 0;
#else
    if (mprotect(ptr, size, PROT_READ | PROT_EXEC) != 0) {
        return false;
    }
#if defined(__GNUC__) || defined(__clang__)
    __builtin___clear_cache(reinterpret_cast<char*>(ptr), reinterpret_cast<char*>(ptr) + size);
#endif
    return true;
#endif
}

[[nodiscard]] inline uint32_t recommend_worker_core(uint32_t worker_index) noexcept {
    const uint32_t cores = std::max<uint32_t>(1u, std::thread::hardware_concurrency());
    return worker_index % cores;
}

[[nodiscard]] inline uint32_t recommend_worker_node(uint32_t worker_index) noexcept {
    const uint32_t nodes = get_num_nodes();
    return (nodes == 0u) ? 0u : (worker_index % nodes);
}

} // namespace dgtl::runtime::numa