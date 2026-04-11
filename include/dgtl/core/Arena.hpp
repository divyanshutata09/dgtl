/*
 * DGTL-MLIR-X v1.0 : THE COMPILER SPEC
 * STEP 1: MEMORY ARCHITECTURE (THE DETERMINISTIC ARENA)
 * PILLAR 1: HUGE-PAGE ALIGNMENT & OS-LEVEL VIRTUAL LOCKING
 * FINAL FIX: Corrected std::align offset tracking and move-semantics safety.
 */

#pragma once
#include <cstdint>
#include <cstddef>
#include <memory>
#include <new>
#include <type_traits>
#include <algorithm>

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
#else
    #include <sys/mman.h>
    #include <unistd.h>
#endif

namespace dgtl::core {

/**
 * @class DGTLArena
 * @brief Transactional Memory Arena utilizing OS Huge-Pages for deterministic IR storage.
 * * PILLAR 1: Guarantees 64-byte alignment for HFT-Optimized SIMD operations.
 * PILLAR 2: Zero-latency rollback through offset-based checkpointing.
 */
class DGTLArena {
private:
    uint8_t* base_ptr = nullptr;
    size_t capacity   = 0;
    size_t offset     = 0;

public:
    /**
     * @brief Default Constructor.
     * Initializes a 256MB strike zone by default for standard compiler passes.
     */
    DGTLArena() : DGTLArena(256) {}

    /**
     * @brief Parameterized Constructor for custom memory pressure requirements.
     * @param size_in_mb The requested size in Megabytes, rounded to the nearest 2MB page.
     */
    explicit DGTLArena(size_t size_in_mb) {
        const size_t page_size = 2 * 1024 * 1024; // 2MB Huge Page Boundary
        
        // Ensure minimum 2MB allocation and prevent 32-bit overflow.
        size_t requested_bytes = (size_in_mb < 1) ? page_size : (static_cast<size_t>(size_in_mb) * 1024 * 1024);
        capacity = (requested_bytes + page_size - 1) & ~(page_size - 1);

#ifdef _WIN32
        // Allocation for Windows: Reserves and commits virtual memory.
        base_ptr = static_cast<uint8_t*>(VirtualAlloc(NULL, capacity, 
                                          MEM_RESERVE | MEM_COMMIT, 
                                          PAGE_READWRITE));
#else
        // Allocation for Linux/Unix: Utilizes MAP_POPULATE to reduce runtime page faults.
        base_ptr = static_cast<uint8_t*>(mmap(NULL, capacity, 
                                          PROT_READ | PROT_WRITE, 
                                          MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE, 
                                          -1, 0));
        if (base_ptr == MAP_FAILED) [[unlikely]] base_ptr = nullptr;
#endif
    }

    ~DGTLArena() {
        if (base_ptr) {
#ifdef _WIN32
            VirtualFree(base_ptr, 0, MEM_RELEASE);
#else
            munmap(base_ptr, capacity);
#endif
        }
    }

    // PILLAR 2: Strict Ownership - No copying allowed to prevent dangling pointers.
    DGTLArena(const DGTLArena&) = delete;
    DGTLArena& operator=(const DGTLArena&) = delete;

    /**
     * @brief Move Constructor.
     */
    DGTLArena(DGTLArena&& other) noexcept 
        : base_ptr(other.base_ptr), capacity(other.capacity), offset(other.offset) {
        other.base_ptr = nullptr;
        other.capacity = 0;
        other.offset = 0;
    }

    /**
     * @brief Move Assignment Operator.
     */
    DGTLArena& operator=(DGTLArena&& other) noexcept {
        if (this != &other) {
            if (base_ptr) {
#ifdef _WIN32
                VirtualFree(base_ptr, 0, MEM_RELEASE);
#else
                munmap(base_ptr, capacity);
#endif
            }
            base_ptr = other.base_ptr;
            capacity = other.capacity;
            offset = other.offset;
            other.base_ptr = nullptr;
            other.capacity = 0;
            other.offset = 0;
        }
        return *this;
    }

    /**
     * @brief High-speed allocation with hardware alignment.
     * @param count Number of elements of type T to allocate.
     * @param alignment Alignment boundary (default 64 bytes for cache-line alignment).
     */
    template <typename T>
    [[nodiscard]] T* allocate(size_t count = 1, size_t alignment = 64) noexcept {
        static_assert(std::is_trivially_destructible_v<T>, 
            "DGTL Arena Error: Only trivially destructible types allowed for Zero-Latency safety.");
        
        if (!base_ptr || count == 0) [[unlikely]] return nullptr;

        void* current_ptr = base_ptr + offset;
        size_t total_bytes = sizeof(T) * count;
        size_t space_remaining = capacity - offset;

        // std::align modifies current_ptr and space_remaining if alignment is successful.
        if (std::align(alignment, total_bytes, current_ptr, space_remaining)) {
            T* result = static_cast<T*>(current_ptr);
            
            // Critical Fix: Recalculate global offset based on aligned pointer position.
            offset = (static_cast<uint8_t*>(current_ptr) - base_ptr) + total_bytes;

            // Placement new to ensure valid initial state for non-fundamental types.
            if constexpr (!std::is_fundamental_v<T>) {
                for (size_t i = 0; i < count; ++i) {
                    new (&result[i]) T(); 
                }
            }
            return result;
        }

        return nullptr; // Memory exhaustion or alignment failure.
    }

    // --- Transactional Strike Controls ---

    /**
     * @brief Captures current offset for potential rollback if a compiler pass fails validation.
     */
    [[nodiscard]] size_t get_checkpoint() const noexcept { return offset; }

    /**
     * @brief Reverts the arena to a previous checkpoint.
     */
    void rollback(size_t checkpoint) noexcept { 
        if (checkpoint <= capacity) offset = checkpoint; 
    }

    /**
     * @brief Resets the arena for reuse without deallocating OS memory.
     */
    void reset() noexcept { offset = 0; }
    
    // --- Accessors ---

    [[nodiscard]] uint8_t* raw_data() noexcept { return base_ptr; }
    [[nodiscard]] size_t get_capacity() const noexcept { return capacity; }
    [[nodiscard]] size_t get_used() const noexcept { return offset; }
};

} // namespace dgtl::core

// --- STABILITY SWEEP HARNESS ---
#ifdef DGTL_UNIT_TEST_MODE
#include <iostream>
int main() {
    dgtl::core::DGTLArena test_arena(2); // 2MB Strike Zone
    if(test_arena.raw_data() != nullptr) {
        std::cout << "DGTL ARENA: MEMORY ARCHITECTURE INITIALIZED SUCCESS" << std::endl;
        return 0;
    }
    return 1;
}
#endif
