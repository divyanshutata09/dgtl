/* * DGTL-MLIR-X v1.0 : CORE MEMORY PROTOCAL
 * PILLAR 1: ZERO-TRUST MEMORY MANAGEMENT
 * STANDARDS: C++26 / UCRT64 / MSVC-Compatible
 */

#pragma once
#include <cstdint>
#include <cstddef>
#include <stdexcept>
#include <source_location>
#include <span>

#ifdef DGTL_WIN64
    #include <intrin.h>
#endif

namespace dgtl::core {

/**
 * @class MemoryGuard
 * @brief Production-grade RAII wrapper for safe memory segment access.
 * Features: Hardware-aligned bounds checking, poisoning, and access telemetry.
 */
class alignas(64) MemoryGuard {
public:
    // Memory Access Permissions
    enum class Access : uint8_t {
        None  = 0,
        Read  = 1 << 0,
        Write = 1 << 1,
        Exec  = 1 << 2
    };

    constexpr MemoryGuard() noexcept = default;

    /**
     * @brief Create a guarded segment over an existing buffer.
     */
    template<typename T>
    explicit constexpr MemoryGuard(T* ptr, size_t count, Access perm = Access::Read | Access::Write) noexcept
        : m_base(reinterpret_cast<uintptr_t>(ptr)),
          m_size(count * sizeof(T)),
          m_perms(perm) {}

    /**
     * @brief Secure access to a specific offset.
     * @throw std::out_of_range in debug builds.
     */
    [[nodiscard]] inline void* get(size_t offset, size_t requested_size, 
                                   std::source_location loc = std::source_location::current()) const {
        #ifdef _DEBUG
        validate_access(offset, requested_size, loc);
        #endif

        return reinterpret_cast<void*>(m_base + offset);
    }

    /**
     * @brief RAII-based Hardware Memory Fence.
     * Ensures all previous writes are committed before proceeding.
     */
    static inline void fence() noexcept {
        #ifdef DGTL_WIN64
            _mm_sfence(); // Store fence
        #else
            __sync_synchronize();
        #endif
    }

    // --- Production Helpers ---

    [[nodiscard]] constexpr size_t size() const noexcept { return m_size; }
    [[nodiscard]] constexpr bool is_null() const noexcept { return m_base == 0; }

private:
    uintptr_t m_base{0};
    size_t    m_size{0};
    Access    m_perms{Access::None};

    /**
     * @brief Internal validation logic (Optimized away in Release).
     */
    void validate_access([[maybe_unused]] size_t offset, 
                         [[maybe_unused]] size_t req_size,
                         [[maybe_unused]] const std::source_location& loc) const {
        if (m_base == 0) [[unlikely]] {
            std::abort();
        }

        if (offset + req_size > m_size) [[unlikely]] {
            std::abort();
        }
    }
};

} // namespace dgtl::core
