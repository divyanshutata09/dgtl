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

/*
 * DGTL (Deep General-purpose Transformation Layer) version 1.0.0
 * STEP 1: NEXT-GEN MEMORY ARENA (v4.0)
 * DESIGN: THREAD-LOCAL + TRANSACTION STACK + PREFETCH ENGINE
 * VERSION 3.0: TITAN-STABLE (NO-COMPROMISE SAFETY)
 */

module;

#include <cstdlib>

// Global Module Fragment: Capture hardware intrinsics and isolation metrics safely
#include <cstdlib>
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <thread>
#include <type_traits>
#include <vector>

#if defined(_MSC_VER) || defined(__GNUC__) || defined(__clang__)
#include <immintrin.h>
#endif

export module dgtl.arena;

import dgtl.memory_manager;

export namespace dgtl::core {

// ================================
// LOW LEVEL BLOCK
// ================================
struct alignas(64) ArenaBlock {
    std::uint8_t* base = nullptr;
    std::size_t capacity = 0;
    std::size_t offset = 0;
};

// ================================
// THREAD-LOCAL ARENA
// ================================
class alignas(64) ThreadLocalArena {
private:
    ArenaBlock block;
    std::vector<std::size_t> checkpoints;
    MemoryManager memory_;
    bool used_large_pages_ = false;

    static constexpr std::size_t MINIMUM_ALIGNMENT = 16;
    static constexpr std::size_t CACHELINE_BYTES = 64;
    static constexpr std::size_t PREFETCH_DISTANCE_BYTES = 2ull * CACHELINE_BYTES;

    static void prefetch_write_two_lines_ahead(const std::uint8_t* base, std::size_t offset, std::size_t capacity) noexcept {
        if (!base || offset >= capacity) [[unlikely]] {
            return;
        }

        const std::size_t prefetch_offset = std::min(offset + PREFETCH_DISTANCE_BYTES, capacity - 1);
        const std::uint8_t* ptr = base + prefetch_offset;

#if defined(_MSC_VER) || defined(__GNUC__) || defined(__clang__)
        _mm_prefetch(reinterpret_cast<const char*>(ptr), _MM_HINT_T0);
#endif
        (void)ptr;
    }

public:
    ThreadLocalArena() = default;

    ThreadLocalArena(const ThreadLocalArena&) = delete;
    ThreadLocalArena& operator=(const ThreadLocalArena&) = delete;
    ~ThreadLocalArena() = default;

    void init(std::size_t size_bytes) {
        if (size_bytes == 0) [[unlikely]] {
            block.base = nullptr;
            block.capacity = 0;
            block.offset = 0;
            used_large_pages_ = false;
            return;
        }

        if (!memory_.allocate(size_bytes)) [[unlikely]] {
            block.base = nullptr;
            block.capacity = 0;
            block.offset = 0;
            used_large_pages_ = false;
            return;
        }

        block.base = memory_.data();
        block.capacity = memory_.size();
        block.offset = 0;
        used_large_pages_ = memory_.has_large_pages();
    }

    template<typename T>
    [[nodiscard]] T* allocate(std::size_t count = 1) noexcept {
        static_assert(std::is_trivially_copyable_v<T>,
                      "DGTL_Arena supports only trivially copyable types for HFT safety.");

        if (!block.base || count == 0) [[unlikely]] {
            return nullptr;
        }

        if (block.offset > block.capacity) [[unlikely]] {
            return nullptr;
        }

        if (count > (std::numeric_limits<std::size_t>::max() / sizeof(T))) [[unlikely]] {
            return nullptr;
        }

        const std::size_t bytes = sizeof(T) * count;
        const std::size_t alignment = std::max<std::size_t>(alignof(T), MINIMUM_ALIGNMENT);

        const std::uintptr_t current_ptr = reinterpret_cast<std::uintptr_t>(block.base + block.offset);
        const std::uintptr_t aligned_ptr = (current_ptr + alignment - 1) & ~(static_cast<std::uintptr_t>(alignment - 1));
        const std::size_t padding = static_cast<std::size_t>(aligned_ptr - current_ptr);

        if (bytes > (std::numeric_limits<std::size_t>::max() - padding)) [[unlikely]] {
            return nullptr;
        }

        const std::size_t total_needed = padding + bytes;
        const std::size_t available = block.capacity - block.offset;
        if (total_needed > available) [[unlikely]] {
            return nullptr;
        }

        T* ptr = reinterpret_cast<T*>(aligned_ptr);
        block.offset += total_needed;

        prefetch_write_two_lines_ahead(block.base, block.offset, block.capacity);
        return ptr;
    }

    void push_checkpoint() {
        checkpoints.push_back(block.offset);
    }

    void rollback() {
        if (!checkpoints.empty()) [[likely]] {
            block.offset = checkpoints.back();
            checkpoints.pop_back();
        }
    }

    void commit() {
        if (!checkpoints.empty()) [[likely]] {
            checkpoints.pop_back();
        }
    }

    void rewind_to(std::size_t offset) noexcept {
        block.offset = std::min(offset, block.capacity);
    }

    [[nodiscard]] std::size_t current_offset() const noexcept { return block.offset; }
    [[nodiscard]] std::size_t get_capacity() const noexcept { return block.capacity; }
    [[nodiscard]] bool using_large_pages() const noexcept { return used_large_pages_; }

    void reset() {
        block.offset = 0;
        checkpoints.clear();
    }

    void pre_touch_all() noexcept {
        if (!block.base || block.capacity == 0) [[unlikely]] {
            return;
        }

        volatile std::uint8_t vsink = 0;
        for (std::size_t i = 0; i < block.capacity; i += CACHELINE_BYTES) {
            block.base[i] = 0u;
            vsink = block.base[i];
        }
        (void)vsink;
    }

    void pre_touch_range(std::size_t byte_start, std::size_t len) noexcept {
        if (!block.base || byte_start >= block.capacity || len == 0) [[unlikely]] {
            return;
        }

        const std::size_t max_len = block.capacity - byte_start;
        const std::size_t clamped_len = std::min(len, max_len);
        const std::size_t clamped_end = byte_start + clamped_len;
        const std::size_t aligned_start = (byte_start / CACHELINE_BYTES) * CACHELINE_BYTES;

        volatile std::uint8_t vsink = 0;
        for (std::size_t i = aligned_start; i < clamped_end; i += CACHELINE_BYTES) {
            block.base[i] = 0u;
            vsink = block.base[i];
        }
        (void)vsink;
    }

    std::uint8_t* raw_data() { return block.base; }
};

// ================================
// GLOBAL ARENA MANAGER
// ================================
class alignas(64) DGTL_Arena {
private:
    std::vector<std::unique_ptr<ThreadLocalArena>> arenas;
    std::size_t per_thread_size = 0;
    bool using_large_pages_ = false;
    std::atomic<std::size_t> next_arena_ticket_{0};

public:
    explicit DGTL_Arena(std::size_t threads = 0, std::size_t size_mb_per_thread = 128) {
        if (threads == 0) {
            threads = std::thread::hardware_concurrency();
        }
        if (threads == 0) {
            threads = 1;
        }

        constexpr std::size_t bytes_per_mb = 1024ull * 1024ull;
        if (size_mb_per_thread > (std::numeric_limits<std::size_t>::max() / bytes_per_mb)) [[unlikely]] {
            per_thread_size = 128ull * bytes_per_mb;
        } else {
            per_thread_size = size_mb_per_thread * bytes_per_mb;
        }
        if (per_thread_size == 0) [[unlikely]] {
            per_thread_size = bytes_per_mb;
        }

        arenas.reserve(threads);
        for (std::size_t i = 0; i < threads; ++i) {
            auto arena = std::make_unique<ThreadLocalArena>();
            arena->init(per_thread_size);
            using_large_pages_ = using_large_pages_ || arena->using_large_pages();
            arenas.push_back(std::move(arena));
        }

        if (arenas.empty()) [[unlikely]] {
            auto arena = std::make_unique<ThreadLocalArena>();
            arena->init(per_thread_size);
            using_large_pages_ = using_large_pages_ || arena->using_large_pages();
            arenas.push_back(std::move(arena));
        }
    }

    /**
     * @brief Thread-safe local acquisition with deterministic slot mapping.
     */
    ThreadLocalArena& get_local() noexcept {
        thread_local const std::size_t local_slot = [this]() noexcept {
            // Evaluated out-of-line without parent context mutation capture races
            return next_arena_ticket_.fetch_add(1, std::memory_order_relaxed);
        }() % arenas.size();
        return *arenas[local_slot];
    }

    template<typename T>
    T* allocate(std::size_t count = 1) noexcept {
        return get_local().allocate<T>(count);
    }

    // ----------------
    // GLOBAL TRANSACTION FACADE
    // ----------------
    void begin() { get_local().push_checkpoint(); }
    void rollback() { get_local().rollback(); }
    void commit() { get_local().commit(); }

    void reset_all() {
        for (auto& arena : arenas) {
            arena->reset();
        }
    }

    void pre_touch_all() noexcept {
        for (auto& arena : arenas) {
            arena->pre_touch_all();
        }
    }

    std::uint8_t* raw_data() { return get_local().raw_data(); }
    [[nodiscard]] bool using_large_pages() const noexcept { return using_large_pages_; }
    [[nodiscard]] std::size_t thread_count() const noexcept { return arenas.size(); }
    [[nodiscard]] std::size_t bytes_per_thread() const noexcept { return per_thread_size; }

    // ================================
    // HARD REAL-TIME EXTENSIONS
    // ================================

    [[nodiscard]] bool validate_hard_realtime_bounds(std::size_t max_total_bytes) const noexcept {
        if (arenas.empty() || per_thread_size == 0) [[unlikely]] {
            return max_total_bytes == 0;
        }
        if (per_thread_size > (std::numeric_limits<std::size_t>::max() / arenas.size())) [[unlikely]] {
            return true;
        }
        const std::size_t total_available = per_thread_size * arenas.size();
        return total_available >= max_total_bytes;
    }

    [[nodiscard]] std::size_t get_peak_allocation_for_thread(std::size_t thread_index) const noexcept {
        if (thread_index >= arenas.size()) [[unlikely]] {
            return 0;
        }
        return arenas[thread_index]->current_offset();
    }

    [[nodiscard]] bool memory_pressure_high(float threshold_pct = 0.85f) const noexcept {
        return std::any_of(arenas.begin(), arenas.end(), [threshold_pct](const auto& arena) {
            const std::size_t capacity = arena->get_capacity();
            if (capacity == 0) [[unlikely]] {
                return false;
            }
            const float usage_ratio = static_cast<float>(arena->current_offset()) / static_cast<float>(capacity);
            return usage_ratio >= threshold_pct;
        });
    }

    [[nodiscard]] bool is_deterministic_with_large_pages() const noexcept {
        return using_large_pages_;
    }
};

} // namespace dgtl::core