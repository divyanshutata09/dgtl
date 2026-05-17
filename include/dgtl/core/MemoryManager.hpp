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
#include <cstdlib>
#include <limits>

#include "dgtl/core/MemoryCapability.hpp"
#include "dgtl/core/VirtualMemory.hpp"

namespace dgtl::core {

class alignas(64) MemoryManager {
public:
    MemoryManager() = default;
    MemoryManager(const MemoryManager&) = delete;
    MemoryManager& operator=(const MemoryManager&) = delete;
    ~MemoryManager() noexcept { release(); }

    // Allocates bytes with the best available page strategy. Returns true on success.
    [[nodiscard]] bool allocate(size_t bytes) noexcept {
        release();
        if (bytes == 0) {
            return false;
        }

        const MemoryCapability cap = probe_memory_capability();
        const size_t granule = cap.page_granule == 0 ? 4096 : cap.page_granule;
        if (bytes > (std::numeric_limits<size_t>::max() - (granule - 1))) {
            return false;
        }
        const size_t aligned = (bytes + granule - 1) & ~(granule - 1);

#ifdef _WIN32
        if (cap.large_pages_ok) {
            base_ = virtual_memory::reserve_large_pages(aligned);
            if (base_) {
                size_ = aligned;
                mode_ = PageMode::Large;
                return true;
            }
        }

        base_ = virtual_memory::reserve_standard_pages(aligned);
        if (base_) {
            size_ = aligned;
            mode_ = PageMode::Standard;
            return true;
        }
#else
        if (cap.large_pages_ok) {
            base_ = virtual_memory::reserve_huge_pages(aligned);
            if (base_) {
                size_ = aligned;
                mode_ = PageMode::Large;
                return true;
            }
        }

        base_ = virtual_memory::reserve_standard_pages(aligned);
        if (base_) {
            size_ = aligned;
            mode_ = PageMode::Standard;
            standard_malloc_backing_ = false;
            return true;
        }

        // Preserve fallback semantics for unprivileged or constrained systems.
        base_ = static_cast<uint8_t*>(std::aligned_alloc(64, aligned));
        if (base_) {
            size_ = aligned;
            mode_ = PageMode::Standard;
            standard_malloc_backing_ = true;
            return true;
        }
#endif
        return false;
    }

    [[nodiscard]] uint8_t* data() const noexcept { return base_; }
    [[nodiscard]] size_t size() const noexcept { return size_; }
    [[nodiscard]] PageMode mode() const noexcept { return mode_; }
    [[nodiscard]] bool has_large_pages() const noexcept { return mode_ == PageMode::Large; }

private:
    void release() noexcept {
        if (!base_) {
            return;
        }

#ifdef _WIN32
        virtual_memory::release_pages(base_);
#else
        if (standard_malloc_backing_) {
            std::free(base_);
        } else {
            virtual_memory::release_pages(base_, size_);
        }
        standard_malloc_backing_ = false;
#endif

        base_ = nullptr;
        size_ = 0;
        mode_ = PageMode::Unknown;
    }

    uint8_t* base_ = nullptr;
    size_t size_ = 0;
    PageMode mode_ = PageMode::Unknown;
#if !defined(_WIN32)
    bool standard_malloc_backing_ = false;
#endif
};

} // namespace dgtl::core
