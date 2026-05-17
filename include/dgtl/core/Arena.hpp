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
 * STEP 1++: NEXT-GEN MEMORY ARENA (v4.0)
 * DESIGN: THREAD-LOCAL + TRANSACTION STACK + PREFETCH ENGINE
 * VERSION 3.0: TITAN-STABLE (NO-COMPROMISE SAFETY)
 */

#pragma once

#include <algorithm>
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

#include "dgtl/core/MemoryManager.hpp"

namespace dgtl::core {

// ================================
// LOW LEVEL BLOCK
// ================================
struct alignas(64) ArenaBlock {
    uint8_t* base = nullptr;
    size_t capacity = 0;
    size_t offset = 0;
};

// ================================
// THREAD-LOCAL ARENA
// ================================
class alignas(64) ThreadLocalArena {
private:
    ArenaBlock block;
    std::vector<size_t> checkpoints;
    MemoryManager memory_;
    bool used_large_pages_ = false;

    // DGTL Standard: default minimum alignment for arena payloads.
    static constexpr size_t MINIMUM_ALIGNMENT = 16;
    static constexpr size_t CACHELINE_BYTES = 64;
    static constexpr size_t PREFETCH_DISTANCE_BYTES = 2ull * CACHELINE_BYTES;

    static void prefetch_write_two_lines_ahead(const uint8_t* base, size_t offset, size_t capacity) noexcept {
        if (!base || offset >= capacity) {
            return;
        }

        const size_t prefetch_offset = std::min(offset + PREFETCH_DISTANCE_BYTES, capacity - 1);
        const uint8_t* ptr = base + prefetch_offset;

#if defined(_MSC_VER) || defined(__GNUC__) || defined(__clang__)
        _mm_prefetch(reinterpret_cast<const char*>(ptr), _MM_HINT_T0);
#endif
    }

public:
    ThreadLocalArena() = default;

    ThreadLocalArena(const ThreadLocalArena&) = delete;
    ThreadLocalArena& operator=(const ThreadLocalArena&) = delete;
    ~ThreadLocalArena() = default;

    void init(size_t size_bytes) {
        if (size_bytes == 0) {
            block.base = nullptr;
            block.capacity = 0;
            block.offset = 0;
            used_large_pages_ = false;
            return;
        }

        if (!memory_.allocate(size_bytes)) {
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
    [[nodiscard]] T* allocate(size_t count = 1) noexcept {
        static_assert(std::is_trivially_copyable_v<T>,
                      "DGTL_Arena supports only trivially copyable types for HFT safety.");

        if (!block.base || count == 0) [[unlikely]] {
            return nullptr;
        }

        if (block.offset > block.capacity) [[unlikely]] {
            return nullptr;
        }

        if (count > (std::numeric_limits<size_t>::max() / sizeof(T))) [[unlikely]] {
            return nullptr;
        }

        const size_t bytes = sizeof(T) * count;
        const size_t alignment = std::max<size_t>(alignof(T), MINIMUM_ALIGNMENT);

        const uintptr_t current_ptr = reinterpret_cast<uintptr_t>(block.base + block.offset);
        const uintptr_t aligned_ptr = (current_ptr + alignment - 1) & ~(static_cast<uintptr_t>(alignment - 1));
        const size_t padding = static_cast<size_t>(aligned_ptr - current_ptr);

        if (bytes > (std::numeric_limits<size_t>::max() - padding)) [[unlikely]] {
            return nullptr;
        }

        const size_t total_needed = padding + bytes;
        const size_t available = block.capacity - block.offset;
        if (total_needed > available) [[unlikely]] {
            return nullptr;
        }

        T* ptr = reinterpret_cast<T*>(aligned_ptr);
        block.offset += total_needed;

        prefetch_write_two_lines_ahead(block.base, block.offset, block.capacity);
        return ptr;
    }

    // ----------------
    // PILLAR 2: TRANSACTIONAL LOGIC
    // ----------------
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

    /**
     * @brief Direct Rewind
     * Allows the Pass Manager to discard speculative analysis results.
     */
    void rewind_to(size_t offset) noexcept {
        block.offset = std::min(offset, block.capacity);
    }

    [[nodiscard]] size_t current_offset() const noexcept { return block.offset; }
    [[nodiscard]] size_t get_capacity() const noexcept { return block.capacity; }
    [[nodiscard]] bool using_large_pages() const noexcept { return used_large_pages_; }

    void reset() {
        block.offset = 0;
        checkpoints.clear();
    }

    /**
     * @brief Pre-touch all arena pages in sequential cache-line stride.
     */
    void pre_touch_all() noexcept {
        if (!block.base || block.capacity == 0) {
            return;
        }

        volatile uint8_t vsink = 0;
        for (size_t i = 0; i < block.capacity; i += CACHELINE_BYTES) {
            block.base[i] = 0u;
            vsink = block.base[i];
        }
        (void)vsink;
    }

    /**
     * @brief Pre-touch a byte subrange [byte_start, byte_start + len) of the arena.
     */
    void pre_touch_range(size_t byte_start, size_t len) noexcept {
        if (!block.base || byte_start >= block.capacity || len == 0) {
            return;
        }

        const size_t max_len = block.capacity - byte_start;
        const size_t clamped_len = std::min(len, max_len);
        const size_t clamped_end = byte_start + clamped_len;
        const size_t aligned_start = (byte_start / CACHELINE_BYTES) * CACHELINE_BYTES;

        volatile uint8_t vsink = 0;
        for (size_t i = aligned_start; i < clamped_end; i += CACHELINE_BYTES) {
            block.base[i] = 0u;
            vsink = block.base[i];
        }
        (void)vsink;
    }

    uint8_t* raw_data() { return block.base; }
};

// ================================
// GLOBAL ARENA MANAGER
// ================================
class alignas(64) DGTL_Arena {
private:
    std::vector<std::unique_ptr<ThreadLocalArena>> arenas;
    size_t per_thread_size = 0;
    bool using_large_pages_ = false;

public:
    explicit DGTL_Arena(size_t threads = 0, size_t size_mb_per_thread = 128) {
        if (threads == 0) {
            threads = std::thread::hardware_concurrency();
        }
        if (threads == 0) {
            threads = 1;
        }

        const size_t bytes_per_mb = 1024ull * 1024ull;
        if (size_mb_per_thread > (std::numeric_limits<size_t>::max() / bytes_per_mb)) {
            per_thread_size = std::numeric_limits<size_t>::max();
        } else {
            per_thread_size = size_mb_per_thread * bytes_per_mb;
        }
        if (per_thread_size == 0) {
            per_thread_size = bytes_per_mb;
        }

        arenas.reserve(threads);
        for (size_t i = 0; i < threads; ++i) {
            auto arena = std::make_unique<ThreadLocalArena>();
            arena->init(per_thread_size);
            using_large_pages_ = using_large_pages_ || arena->using_large_pages();
            arenas.push_back(std::move(arena));
        }

        if (arenas.empty()) {
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
        thread_local const size_t thread_hash = std::hash<std::thread::id>{}(std::this_thread::get_id());
        const size_t local_slot = thread_hash % arenas.size();
        return *arenas[local_slot];
    }

    template<typename T>
    T* allocate(size_t count = 1) noexcept {
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

    /**
     * @brief Pre-touch all per-thread arena pages to commit physical memory.
     */
    void pre_touch_all() noexcept {
        for (auto& arena : arenas) {
            arena->pre_touch_all();
        }
    }

    uint8_t* raw_data() { return get_local().raw_data(); }
    [[nodiscard]] bool using_large_pages() const noexcept { return using_large_pages_; }

    // ================================
    // HARD REAL-TIME EXTENSIONS
    // ================================

    [[nodiscard]] bool validate_hard_realtime_bounds(size_t max_total_bytes) const noexcept {
        if (arenas.empty() || per_thread_size == 0) {
            return max_total_bytes == 0;
        }
        if (per_thread_size > (std::numeric_limits<size_t>::max() / arenas.size())) {
            return true;
        }
        const size_t total_available = per_thread_size * arenas.size();
        return total_available >= max_total_bytes;
    }

    [[nodiscard]] size_t get_peak_allocation_for_thread(size_t thread_index) const noexcept {
        if (thread_index >= arenas.size()) {
            return 0;
        }
        return arenas[thread_index]->current_offset();
    }

    [[nodiscard]] bool memory_pressure_high(float threshold_pct = 0.85f) const noexcept {
        for (const auto& arena : arenas) {
            const size_t capacity = arena->get_capacity();
            if (capacity == 0) {
                continue;
            }
            const float usage_ratio = static_cast<float>(arena->current_offset()) / static_cast<float>(capacity);
            if (usage_ratio >= threshold_pct) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] bool is_deterministic_with_large_pages() const noexcept {
        return using_large_pages_;
    }
};

} // namespace dgtl::core
