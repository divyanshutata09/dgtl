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
#include <cstdlib>
#include <limits>

export module dgtl.memory_manager;

// Dependency Ordered Module Imports
import dgtl.virtual_memory;
import dgtl.memory_capability;

export namespace dgtl::core {

class alignas(64) MemoryManager {
public:
    MemoryManager() = default;
    MemoryManager(const MemoryManager&) = delete;
    MemoryManager& operator=(const MemoryManager&) = delete;
    ~MemoryManager() noexcept { release(); }

    // Allocates bytes with the best available page strategy. Returns true on success.
    [[nodiscard]] bool allocate(std::size_t bytes) noexcept {
        release();
        if (bytes == 0u) [[unlikely]] {
            return false;
        }

        const MemoryCapability cap = probe_memory_capability();
        const std::size_t granule = cap.page_granule == 0u ? cap.standard_page_size : cap.page_granule;
        const std::size_t aligned = virtual_memory::align_up(bytes, granule);
        if (aligned < bytes || aligned == 0u) [[unlikely]] {
            return false;
        }

#ifdef _WIN32
        if (cap.large_pages_ok) {
            base_ = virtual_memory::reserve_large_pages(aligned);
            if (base_) [[likely]] {
                size_ = aligned;
                mode_ = PageMode::Large;
                return true;
            }
        }

        base_ = virtual_memory::reserve_standard_pages(aligned);
        if (base_) [[likely]] {
            size_ = aligned;
            mode_ = PageMode::Standard;
            return true;
        }
#else
        if (cap.large_pages_ok) {
            base_ = virtual_memory::reserve_huge_pages(aligned);
            if (base_) [[likely]] {
                size_ = aligned;
                mode_ = PageMode::Large;
                return true;
            }
        }

        base_ = virtual_memory::reserve_standard_pages(aligned);
        if (base_) [[likely]] {
            size_ = aligned;
            mode_ = PageMode::Standard;
            standard_malloc_backing_ = false;
            return true;
        }

        // Preserve fallback semantics for unprivileged or constrained systems.
        const std::size_t fallback_aligned = virtual_memory::align_up(aligned, 64u);
        if (fallback_aligned < aligned || fallback_aligned == 0u) [[unlikely]] {
            return false;
        }
        base_ = static_cast<std::uint8_t*>(std::aligned_alloc(64u, fallback_aligned));
        if (base_) [[likely]] {
            size_ = fallback_aligned;
            mode_ = PageMode::Standard;
            standard_malloc_backing_ = true;
            return true;
        }
#endif
        return false;
    }

    [[nodiscard]] std::uint8_t* data() const noexcept { return base_; }
    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    [[nodiscard]] PageMode mode() const noexcept { return mode_; }
    [[nodiscard]] bool has_large_pages() const noexcept { return mode_ == PageMode::Large; }

private:
    void release() noexcept {
        if (!base_) [[likely]] {
            return;
        }

#ifdef _WIN32
        // Fixed: Synced to support unified two-parameter custom layout signature 
        virtual_memory::release_pages(base_, size_);
#else
        if (standard_malloc_backing_) {
            std::free(base_);
        } else {
            virtual_memory::release_pages(base_, size_);
        }
        standard_malloc_backing_ = false;
#endif

        base_ = nullptr;
        size_ = 0u;
        mode_ = PageMode::Unknown;
    }

    std::uint8_t* base_ = nullptr;
    std::size_t size_ = 0u;
    PageMode mode_ = PageMode::Unknown;
#if !defined(_WIN32)
    bool standard_malloc_backing_ = false;
#endif
};

} // namespace dgtl::core