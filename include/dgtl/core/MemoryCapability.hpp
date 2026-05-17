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

#include "dgtl/core/VirtualMemory.hpp"

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
#endif

namespace dgtl::core {

enum class PageMode : uint8_t {
    Unknown  = 0,
    Large    = 1,
    Standard = 2
};

struct MemoryCapability {
    PageMode page_mode      = PageMode::Unknown;
    bool     large_pages_ok = false;
    size_t   page_granule   = 4096;
};

// Probes once, caches result. Thread-safe via function-local static.
[[nodiscard]] inline MemoryCapability probe_memory_capability() noexcept {
    static MemoryCapability cached = []() noexcept -> MemoryCapability {
        MemoryCapability cap;
#ifdef _WIN32
        HANDLE token = nullptr;
        bool privilege_ok = false;

        if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token)) {
            LUID luid{};
            if (LookupPrivilegeValueA(nullptr, "SeLockMemoryPrivilege", &luid)) {
                TOKEN_PRIVILEGES tp{};
                tp.PrivilegeCount = 1;
                tp.Privileges[0].Luid = luid;
                tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
                if (AdjustTokenPrivileges(token, FALSE, &tp, sizeof(tp), nullptr, nullptr) &&
                    GetLastError() == ERROR_SUCCESS) {
                    privilege_ok = true;
                }
            }
            CloseHandle(token);
        }

        const size_t large_page_min = virtual_memory::large_page_minimum();
        if (privilege_ok && large_page_min != 0) {
            cap.large_pages_ok = true;
            cap.page_mode = PageMode::Large;
            cap.page_granule = large_page_min;
        } else {
            cap.large_pages_ok = false;
            cap.page_mode = PageMode::Standard;
            cap.page_granule = 4096;
        }
#else
        uint8_t* probe = virtual_memory::reserve_huge_pages(virtual_memory::kHugePageBytes);
        if (probe) {
            virtual_memory::release_pages(probe, virtual_memory::kHugePageBytes);
            cap.large_pages_ok = true;
            cap.page_mode = PageMode::Large;
            cap.page_granule = virtual_memory::kHugePageBytes;
        } else {
            cap.large_pages_ok = false;
            cap.page_mode = PageMode::Standard;
            cap.page_granule = 4096;
        }
#endif
        return cap;
    }();

    return cached;
}

} // namespace dgtl::core
