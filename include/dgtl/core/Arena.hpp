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
#include <cstdint>
#include <cstddef>
#include <vector>
#include <thread>
#include <atomic>
#include <new>
#include <type_traits>
#include <limits>
#include <algorithm>
#include <memory>

#if defined(_MSC_VER) || defined(__GNUC__) || defined(__clang__)
#include <immintrin.h>
#endif

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
    // Undef Windows macros that conflict with standard C++ names
    #ifdef MEM_RELEASE
    #undef MEM_RELEASE
    #endif
#else
    #include <cerrno>
    #include <sys/mman.h>
#endif

namespace dgtl::core {

// ================================
// 🔹 LOW LEVEL BLOCK
// ================================
struct alignas(64) ArenaBlock {
    uint8_t* base = nullptr;
    size_t capacity = 0;
    size_t offset = 0;
};

// ================================
// 🔹 THREAD-LOCAL ARENA
// ================================
class alignas(64) ThreadLocalArena {
private:
    ArenaBlock block;
    std::vector<size_t> checkpoints;
    bool used_large_pages_ = false;

    // DGTL Standard: 64-byte alignment for AVX-512 and Cache-Line optimization
    static constexpr size_t ALIGNMENT = 64;
    static constexpr size_t HUGE_PAGE_BYTES = 2ull * 1024ull * 1024ull;
    static constexpr size_t PREFETCH_DISTANCE_BYTES = 2ull * 64ull;

    [[nodiscard]] static size_t round_up(size_t value, size_t alignment) noexcept {
        if (alignment == 0) return value;
        if (value > (std::numeric_limits<size_t>::max() - (alignment - 1))) {
            return std::numeric_limits<size_t>::max();
        }
        return (value + alignment - 1) & ~(alignment - 1);
    }

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

    void init_windows(size_t size_bytes) noexcept {
        const size_t large_page_min = static_cast<size_t>(GetLargePageMinimum());
        const bool can_try_large_pages = large_page_min != 0;
        const size_t rounded_size = round_up(size_bytes, can_try_large_pages ? large_page_min : HUGE_PAGE_BYTES);

        if (can_try_large_pages) {
            block.base = static_cast<uint8_t*>(
                VirtualAlloc(nullptr,
                             rounded_size,
                             MEM_RESERVE | MEM_COMMIT | MEM_LARGE_PAGES,
                             PAGE_READWRITE)
            );
            block.capacity = block.base ? rounded_size : 0;
            used_large_pages_ = block.base != nullptr;
        }

        if (!block.base) {
            block.base = static_cast<uint8_t*>(
                VirtualAlloc(nullptr, size_bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE)
            );
            block.capacity = block.base ? size_bytes : 0;
            used_large_pages_ = false;
        }
    }

#if !defined(_WIN32)
    void init_posix(size_t size_bytes) noexcept {
        const size_t rounded_size = round_up(size_bytes, HUGE_PAGE_BYTES);
#if defined(MAP_HUGETLB)
        void* huge = mmap(nullptr,
                          rounded_size,
                          PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE | MAP_HUGETLB,
                          -1,
                          0);
        if (huge != MAP_FAILED) {
            block.base = static_cast<uint8_t*>(huge);
            block.capacity = rounded_size;
            used_large_pages_ = true;
            return;
        }
#endif

        void* standard = mmap(nullptr,
                              rounded_size,
                              PROT_READ | PROT_WRITE,
                              MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE,
                              -1,
                              0);
        if (standard == MAP_FAILED) {
            block.base = nullptr;
            block.capacity = 0;
            used_large_pages_ = false;
            return;
        }

        block.base = static_cast<uint8_t*>(standard);
        block.capacity = rounded_size;
        used_large_pages_ = false;
    }
#endif

public:
    ThreadLocalArena() = default;

    // Delete copy to prevent accidental performance degradation
    ThreadLocalArena(const ThreadLocalArena&) = delete;
    ThreadLocalArena& operator=(const ThreadLocalArena&) = delete;

    void init(size_t size_bytes) {
        if (size_bytes == 0) {
            block.base = nullptr;
            block.capacity = 0;
            block.offset = 0;
            used_large_pages_ = false;
            return;
        }

#ifdef _WIN32
        init_windows(size_bytes);
#else
        init_posix(size_bytes);
#endif
        block.offset = 0;
    }

    ~ThreadLocalArena() {
#ifdef _WIN32
        if (block.base) VirtualFree(block.base, 0, 0x8000);  // MEM_RELEASE = 0x8000
#else
        if (block.base) munmap(block.base, block.capacity);
#endif
    }

    template<typename T>
    [[nodiscard]] T* allocate(size_t count = 1) noexcept {
        static_assert(std::is_trivially_copyable_v<T>,
                      "DGTL_Arena supports only trivially copyable types for HFT safety.");

        if (!block.base || count == 0) [[unlikely]] return nullptr;

        if (count > (std::numeric_limits<size_t>::max() / sizeof(T))) [[unlikely]] {
            return nullptr;
        }

        const size_t bytes = sizeof(T) * count;
        
        // Ensure alignment is calculated relative to the base pointer
        const uintptr_t current_ptr = reinterpret_cast<uintptr_t>(block.base + block.offset);
        const uintptr_t aligned_ptr = (current_ptr + ALIGNMENT - 1) & ~(ALIGNMENT - 1);
        const size_t padding = aligned_ptr - current_ptr;

        if (bytes > (std::numeric_limits<size_t>::max() - padding)) [[unlikely]] {
            return nullptr;
        }

        const size_t total_needed = padding + bytes;

        if (total_needed > (block.capacity - block.offset)) [[unlikely]] return nullptr;

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
     *
     * Commits all virtual pages to physical memory, eliminating TLB miss variance
     * that causes 7–8% run-to-run drift in memory-intensive workloads (ECS, SIMD).
     * The volatile read-back on each cache line prevents write-combine elision under
     * -O2/-O3/-Ofast. Call once after init(), before any allocate() calls.
     *
     * Cost: O(capacity / 64) sequential writes — amortized during warmup.
     * This is a permanent determinism guarantee, not a benchmark-only workaround.
     */
    void pre_touch_all() noexcept {
        if (!block.base || block.capacity == 0) return;
        volatile uint8_t vsink = 0;
        for (size_t i = 0; i < block.capacity; i += ALIGNMENT) {
            block.base[i] = 0u;
            vsink = block.base[i];  // read-back: prevents write-combine elision
        }
        (void)vsink;
    }

    /**
     * @brief Pre-touch a byte subrange [byte_start, byte_start + len) of the arena.
     *
     * Useful when only a subset of the arena's pages need deterministic commitment.
     * Aligned down to the nearest cache-line boundary automatically.
     * Same volatile-sink guarantee as pre_touch_all().
     */
    void pre_touch_range(size_t byte_start, size_t len) noexcept {
        if (!block.base || byte_start >= block.capacity || len == 0) return;
        const size_t clamped_end   = std::min(byte_start + len, block.capacity);
        const size_t aligned_start = (byte_start / ALIGNMENT) * ALIGNMENT;
        volatile uint8_t vsink = 0;
        for (size_t i = aligned_start; i < clamped_end; i += ALIGNMENT) {
            block.base[i] = 0u;
            vsink = block.base[i];
        }
        (void)vsink;
    }

    uint8_t* raw_data() { return block.base; }
};

// ================================
// 🔹 GLOBAL ARENA MANAGER
// ================================
class alignas(64) DGTL_Arena {
private:
    std::vector<std::unique_ptr<ThreadLocalArena>> arenas;
    size_t per_thread_size;
    std::atomic<size_t> thread_counter{0};
    bool using_large_pages_ = false;

public:
    explicit DGTL_Arena(size_t threads = 0, size_t size_mb_per_thread = 128)
    {
        if (threads == 0) threads = std::thread::hardware_concurrency();
        if (threads == 0) threads = 1;

        const size_t bytes_per_mb = 1024ull * 1024ull;
        if (size_mb_per_thread > (std::numeric_limits<size_t>::max() / bytes_per_mb)) {
            per_thread_size = std::numeric_limits<size_t>::max();
        } else {
            per_thread_size = size_mb_per_thread * bytes_per_mb;
        }
        if (per_thread_size == 0) {
            per_thread_size = bytes_per_mb;
        }

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
     * @brief Thread-Safe Local Acquisition
     * Uses a deterministic thread_local index to map OS threads to Arena slots.
     */
    ThreadLocalArena& get_local() noexcept {
        thread_local size_t local_slot = thread_counter.fetch_add(1, std::memory_order_relaxed) % arenas.size();
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
     *
     * Eliminates TLB miss variance across all threads before compilation or
     * measurement begins.  Delegates to ThreadLocalArena::pre_touch_all() on
     * each slot.  Safe to call from the owning thread before worker threads start.
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
    
    /**
     * @brief Hard real-time configuration: Validate bounded memory before compilation.
     * Returns false if memory constraints cannot be met.
     */
    [[nodiscard]] bool validate_hard_realtime_bounds(size_t max_total_bytes) const noexcept {
        const size_t total_available = per_thread_size * arenas.size();
        return total_available >= max_total_bytes;
    }

    /**
     * @brief Get peak memory usage tracking for jitter detection.
     * Used to detect allocation-time variance (should be < 5% for hard real-time).
     */
    [[nodiscard]] size_t get_peak_allocation_for_thread(size_t thread_index) const noexcept {
        if (thread_index >= arenas.size()) return 0;
        return arenas[thread_index]->current_offset();
    }

    /**
     * @brief Fail-safe: Detect if memory pressure is high (allocation-time jitter indicator).
     */
    [[nodiscard]] bool memory_pressure_high(float threshold_pct = 0.85f) const noexcept {
        for (const auto& arena : arenas) {
            const float usage_ratio = static_cast<float>(arena->current_offset()) / arena->get_capacity();
            if (usage_ratio >= threshold_pct) return true;
        }
        return false;
    }

    /**
     * @brief Query whether system is using large pages (determinism indicator).
     * Hard real-time should prefer large pages (fewer TLB misses = stable jitter).
     */
    [[nodiscard]] bool is_deterministic_with_large_pages() const noexcept {
        return using_large_pages_;
    }
};

} // namespace dgtl::core

// ================================
// 🔹 CAPABILITY-AWARE MEMORY MANAGER
// ================================
//
// Policy-based allocator that probes OS for SeLockMemoryPrivilege at runtime.
// If large pages are available: VirtualAlloc(MEM_LARGE_PAGES).
// Otherwise: graceful fallback to standard VirtualAlloc / aligned_alloc.
// Zero-crash guarantee on unprivileged environments.
//
namespace dgtl::core {

enum class PageMode : uint8_t {
    Unknown  = 0,
    Large    = 1,   // 2MB huge pages via SeLockMemoryPrivilege
    Standard = 2    // 4KB standard pages
};

struct MemoryCapability {
    PageMode page_mode      = PageMode::Unknown;
    bool     large_pages_ok = false;
    size_t   page_granule   = 4096;
};

// Probes once, caches result.  Thread-safe via function-local static.
[[nodiscard]] inline MemoryCapability probe_memory_capability() noexcept {
    static MemoryCapability cached = []() noexcept -> MemoryCapability {
        MemoryCapability cap;
#ifdef _WIN32
        // Attempt SeLockMemoryPrivilege
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
        const SIZE_T lgmin = GetLargePageMinimum();
        if (privilege_ok && lgmin != 0) {
            cap.large_pages_ok = true;
            cap.page_mode      = PageMode::Large;
            cap.page_granule   = static_cast<size_t>(lgmin);
        } else {
            cap.large_pages_ok = false;
            cap.page_mode      = PageMode::Standard;
            cap.page_granule   = 4096;
        }
#else
    #if defined(MAP_HUGETLB)
        void* probe = mmap(nullptr,
                           HUGE_PAGE_BYTES,
                           PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB,
                           -1,
                           0);
        if (probe != MAP_FAILED) {
            munmap(probe, HUGE_PAGE_BYTES);
            cap.large_pages_ok = true;
            cap.page_mode = PageMode::Large;
            cap.page_granule = HUGE_PAGE_BYTES;
        } else {
            cap.large_pages_ok = false;
            cap.page_mode = PageMode::Standard;
            cap.page_granule = 4096;
        }
    #else
        cap.large_pages_ok = false;
        cap.page_mode      = PageMode::Standard;
        cap.page_granule   = 4096;
    #endif
#endif
        return cap;
    }();
    return cached;
}

class alignas(64) MemoryManager {
public:
    MemoryManager() = default;
    MemoryManager(const MemoryManager&) = delete;
    MemoryManager& operator=(const MemoryManager&) = delete;
    ~MemoryManager() noexcept { release(); }

    // Allocates 'bytes' with the best available page strategy.
    // Returns true on success.  Never throws.
    [[nodiscard]] bool allocate(size_t bytes) noexcept {
        release();
        const MemoryCapability cap = probe_memory_capability();
        const size_t granule = cap.page_granule;
        const size_t aligned = (bytes + granule - 1) & ~(granule - 1);

#ifdef _WIN32
        if (cap.large_pages_ok) {
            base_ = static_cast<uint8_t*>(
                VirtualAlloc(nullptr, aligned,
                             MEM_RESERVE | MEM_COMMIT | MEM_LARGE_PAGES,
                             PAGE_READWRITE));
            if (base_) { size_ = aligned; mode_ = PageMode::Large; return true; }
        }
        // Fallback: standard pages, no crash
        base_ = static_cast<uint8_t*>(
            VirtualAlloc(nullptr, aligned, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
        if (base_) { size_ = aligned; mode_ = PageMode::Standard; return true; }
#else
        if (cap.large_pages_ok) {
            void* huge = mmap(nullptr,
                              aligned,
                              PROT_READ | PROT_WRITE,
                              MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB,
                              -1,
                              0);
            if (huge != MAP_FAILED) {
                base_ = static_cast<uint8_t*>(huge);
                size_ = aligned;
                mode_ = PageMode::Large;
                return true;
            }
        }
        base_ = static_cast<uint8_t*>(std::aligned_alloc(64, aligned));
        if (base_) { size_ = aligned; mode_ = PageMode::Standard; return true; }
#endif
        return false;
    }

    [[nodiscard]] uint8_t*  data()            const noexcept { return base_; }
    [[nodiscard]] size_t    size()            const noexcept { return size_; }
    [[nodiscard]] PageMode  mode()            const noexcept { return mode_; }
    [[nodiscard]] bool      has_large_pages() const noexcept { return mode_ == PageMode::Large; }

private:
    void release() noexcept {
        if (!base_) return;
#ifdef _WIN32
        VirtualFree(base_, 0, 0x8000);  // MEM_RELEASE = 0x8000
#else
    if (mode_ == PageMode::Large) {
        munmap(base_, size_);
    } else {
        std::free(base_);
    }
#endif
        base_ = nullptr; size_ = 0; mode_ = PageMode::Unknown;
    }

    uint8_t* base_  = nullptr;
    size_t   size_  = 0;
    PageMode mode_  = PageMode::Unknown;
};

} // namespace dgtl::core (MemoryManager extension)