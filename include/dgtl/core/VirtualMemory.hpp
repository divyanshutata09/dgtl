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

#include <cstddef>
#include <cstdint>

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
#endif

namespace dgtl::core::virtual_memory {

inline constexpr size_t kHugePageBytes = 2ull * 1024ull * 1024ull;

#ifdef _WIN32

[[nodiscard]] inline size_t large_page_minimum() noexcept {
    return static_cast<size_t>(GetLargePageMinimum());
}

[[nodiscard]] inline uint8_t* reserve_large_pages(size_t bytes) noexcept {
    return static_cast<uint8_t*>(
        VirtualAlloc(nullptr,
                     bytes,
                     MEM_RESERVE | MEM_COMMIT | MEM_LARGE_PAGES,
                     PAGE_READWRITE)
    );
}

[[nodiscard]] inline uint8_t* reserve_standard_pages(size_t bytes) noexcept {
    return static_cast<uint8_t*>(
        VirtualAlloc(nullptr,
                     bytes,
                     MEM_RESERVE | MEM_COMMIT,
                     PAGE_READWRITE)
    );
}

inline void release_pages(uint8_t* base) noexcept {
    if (!base) {
        return;
    }
    VirtualFree(base, 0, 0x8000);  // MEM_RELEASE = 0x8000
}

#else

#if defined(MAP_POPULATE)
inline constexpr int kPopulateFlag = MAP_POPULATE;
#else
inline constexpr int kPopulateFlag = 0;
#endif

[[nodiscard]] inline uint8_t* reserve_huge_pages(size_t bytes) noexcept {
#if defined(MAP_HUGETLB)
    void* ptr = mmap(nullptr,
                     bytes,
                     PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB | kPopulateFlag,
                     -1,
                     0);
    return ptr == MAP_FAILED ? nullptr : static_cast<uint8_t*>(ptr);
#else
    (void)bytes;
    return nullptr;
#endif
}

[[nodiscard]] inline uint8_t* reserve_standard_pages(size_t bytes) noexcept {
    void* ptr = mmap(nullptr,
                     bytes,
                     PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS | kPopulateFlag,
                     -1,
                     0);
    return ptr == MAP_FAILED ? nullptr : static_cast<uint8_t*>(ptr);
}

inline void release_pages(uint8_t* base, size_t bytes) noexcept {
    if (!base || bytes == 0) {
        return;
    }
    munmap(base, bytes);
}

#endif

} // namespace dgtl::core::virtual_memory
