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

#include <cstdlib>

// Global Module Fragment: Capture operating system allocations cleanly
#include <cstddef>
#include <cstdint>
#include <algorithm>

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
    #ifdef CONST
        #undef CONST
    #endif
#else
    #include <sys/mman.h>
    #include <unistd.h>
#endif

export module dgtl.virtual_memory;

export namespace dgtl::core::virtual_memory {

inline constexpr std::size_t kHugePageBytes = 2ull * 1024ull * 1024ull;

[[nodiscard]] inline constexpr std::size_t align_up(std::size_t value, std::size_t granule) noexcept {
    if (granule == 0u) [[unlikely]] {
        return value;
    }
    // Fast path optimization for power-of-two memory page granularities (4KB, 2MB, etc.)
    if ((granule & (granule - 1ull)) == 0ull) [[likely]] {
        const std::size_t mask = granule - 1ull;
        return (value + mask) & ~mask;
    }
    const std::size_t rem = value % granule;
    if (rem == 0u) {
        return value;
    }
    const std::size_t add = granule - rem;
    if (value > static_cast<std::size_t>(-1) - add) [[unlikely]] {
        return value;
    }
    return value + add;
}

#ifdef _WIN32

[[nodiscard]] inline std::size_t standard_page_size() noexcept {
    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    return si.dwPageSize ? static_cast<std::size_t>(si.dwPageSize) : 4096u;
}

[[nodiscard]] inline std::size_t large_page_minimum() noexcept {
    return static_cast<std::size_t>(GetLargePageMinimum());
}

[[nodiscard]] inline std::uint8_t* reserve_large_pages(std::size_t bytes) noexcept {
    return static_cast<std::uint8_t*>(
        VirtualAlloc(nullptr,
                     bytes,
                     MEM_RESERVE | MEM_COMMIT | MEM_LARGE_PAGES,
                     PAGE_READWRITE)
    );
}

[[nodiscard]] inline std::uint8_t* reserve_standard_pages(std::size_t bytes) noexcept {
    return static_cast<std::uint8_t*>(
        VirtualAlloc(nullptr,
                     bytes,
                     MEM_RESERVE | MEM_COMMIT,
                     PAGE_READWRITE)
    );
}

// Unified signature format to provide compiler parity across targets
inline void release_pages(std::uint8_t* base, [[maybe_unused]] std::size_t bytes) noexcept {
    if (!base) [[unlikely]] {
        return;
    }
    VirtualFree(base, 0u, 0x8000u);  // MEM_RELEASE = 0x8000
}

#else

#if defined(MAP_POPULATE)
inline constexpr int kPopulateFlag = MAP_POPULATE;
#else
inline constexpr int kPopulateFlag = 0;
#endif

[[nodiscard]] inline std::size_t standard_page_size() noexcept {
    const long ps = sysconf(_SC_PAGESIZE);
    return ps > 0 ? static_cast<std::size_t>(ps) : 4096u;
}

[[nodiscard]] inline std::uint8_t* reserve_huge_pages(std::size_t bytes) noexcept {
#if defined(MAP_HUGETLB)
    int flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB;
#if defined(MAP_POPULATE)
    flags |= MAP_POPULATE;
#endif
    void* ptr = mmap(nullptr, bytes, PROT_READ | PROT_WRITE, flags, -1, 0);
    return ptr == MAP_FAILED ? nullptr : static_cast<std::uint8_t*>(ptr);
#else
    (void)bytes;
    return nullptr;
#endif
}

[[nodiscard]] inline std::uint8_t* reserve_standard_pages(std::size_t bytes) noexcept {
    int flags = MAP_PRIVATE | MAP_ANONYMOUS;
#if defined(MAP_POPULATE)
    flags |= MAP_POPULATE;
#endif
    void* ptr = mmap(nullptr, bytes, PROT_READ | PROT_WRITE, flags, -1, 0);
    return ptr == MAP_FAILED ? nullptr : static_cast<std::uint8_t*>(ptr);
}

inline void release_pages(std::uint8_t* base, std::size_t bytes) noexcept {
    if (!base || bytes == 0u) [[unlikely]] {
        return;
    }
    munmap(base, bytes);
}

#endif

} // namespace dgtl::core::virtual_memory
