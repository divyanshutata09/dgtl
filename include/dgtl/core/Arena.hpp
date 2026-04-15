/*
 * DGTL-MLIR-X v1.0
 * STEP 1++: NEXT-GEN MEMORY ARENA (v4.0)
 * DESIGN: THREAD-LOCAL + TRANSACTION STACK + PREFETCH ENGINE
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

#ifdef _WIN32
    #include <windows.h>
#else
    #include <sys/mman.h>
#endif

namespace dgtl::core {

// ================================
// 🔹 LOW LEVEL BLOCK
// ================================
struct ArenaBlock {
    uint8_t* base = nullptr;
    size_t capacity = 0;
    size_t offset = 0;
};

// ================================
// 🔹 THREAD-LOCAL ARENA
// ================================
class ThreadLocalArena {
private:
    ArenaBlock block;
    std::vector<size_t> checkpoints;

    static constexpr size_t ALIGNMENT = 64;

public:
    ThreadLocalArena() = default;

    void init(size_t size_bytes) {
#ifdef _WIN32
        block.base = static_cast<uint8_t*>(
            VirtualAlloc(nullptr, size_bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE)
        );
#else
        block.base = static_cast<uint8_t*>(
            mmap(nullptr, size_bytes, PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0)
        );
        if (block.base == MAP_FAILED) block.base = nullptr;
#endif
        block.capacity = size_bytes;
        block.offset = 0;
    }

    ~ThreadLocalArena() {
#ifdef _WIN32
        if (block.base) VirtualFree(block.base, 0, MEM_RELEASE);
#else
        if (block.base) munmap(block.base, block.capacity);
#endif
    }

    template<typename T>
    T* allocate(size_t count = 1) noexcept {
        static_assert(std::is_trivially_default_constructible_v<T>,
                      "DGTL_Arena supports only trivially default-constructible types");
        static_assert(std::is_trivially_destructible_v<T>,
                      "DGTL_Arena supports only trivially destructible types");

        if (!block.base || count == 0) return nullptr;

        if (count > (std::numeric_limits<size_t>::max() / sizeof(T)))
            return nullptr;

        size_t bytes = sizeof(T) * count;

        uintptr_t raw = reinterpret_cast<uintptr_t>(block.base + block.offset);
        uintptr_t aligned = (raw + ALIGNMENT - 1) & ~(ALIGNMENT - 1);

        size_t new_offset = (aligned - reinterpret_cast<uintptr_t>(block.base)) + bytes;

        if (new_offset > block.capacity) return nullptr;

        T* ptr = reinterpret_cast<T*>(aligned);
        block.offset = new_offset;

        // 🔥 PREFETCH next cache line
#if defined(__GNUC__) || defined(__clang__)
        __builtin_prefetch(ptr, 1, 3);
#endif

        return ptr;
    }

    // ----------------
    // TRANSACTION STACK
    // ----------------
    void push_checkpoint() {
        checkpoints.push_back(block.offset);
    }

    void rollback() {
        if (!checkpoints.empty()) {
            block.offset = checkpoints.back();
            checkpoints.pop_back();
        }
    }

    void commit() {
        if (!checkpoints.empty()) {
            checkpoints.pop_back();
        }
    }

    void reset() {
        block.offset = 0;
        checkpoints.clear();
    }

    uint8_t* raw_data() { return block.base; }
};

// ================================
// 🔹 GLOBAL ARENA MANAGER
// ================================
class DGTL_Arena {
private:
    std::vector<ThreadLocalArena> arenas;
    size_t per_thread_size;
    std::atomic<size_t> next_thread_slot{0};

    [[nodiscard]] size_t acquire_thread_slot() noexcept {
        const size_t arena_count = arenas.size();
        if (arena_count == 0) return 0;
        return next_thread_slot.fetch_add(1, std::memory_order_relaxed) % arena_count;
    }

public:
    explicit DGTL_Arena(size_t threads = std::thread::hardware_concurrency(),
                         size_t size_mb_per_thread = 64)
    {
        if (threads == 0) {
            threads = 1;
        }
        if (size_mb_per_thread == 0) {
            size_mb_per_thread = 1;
        }

        per_thread_size = size_mb_per_thread * 1024ull * 1024ull;

        arenas.resize(threads);

        for (auto& arena : arenas) {
            arena.init(per_thread_size);
        }
    }

    ThreadLocalArena& get_local() noexcept {
        thread_local size_t tid = acquire_thread_slot();
        return arenas[tid];
    }

    template<typename T>
    T* allocate(size_t count = 1) noexcept {
        return get_local().template allocate<T>(count);
    }

    // ----------------
    // TRANSACTION API
    // ----------------
    void begin() {
        get_local().push_checkpoint();
    }

    void rollback() {
        get_local().rollback();
    }

    void commit() {
        get_local().commit();
    }

    void reset_all() {
        for (auto& arena : arenas) {
            arena.reset();
        }
    }

    uint8_t* raw_data() { return get_local().raw_data(); }
};

} // namespace dgtl::core

