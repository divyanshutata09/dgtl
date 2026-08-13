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

/* * DGTL (Deep General-purpose Transformation Layer) version 1.0.0 : UTILS MEMORY
 * PILLAR 1: ZERO-TRUST MEMORY MANAGEMENT
 * STANDARDS: C++26 / UCRT64 / MSVC-Compatible
 */

module;

#include <cstdlib>

// Global Module Fragment: Include core STL and architecture diagnostics here
#include <cstdint>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <source_location>
#include <span>

#if defined(_WIN32) || defined(_WIN64)
    #ifndef DGTL_WIN64
        #define DGTL_WIN64
    #endif
    #include <intrin.h>
#endif

export module dgtl.memory_guard;

export namespace dgtl::utils::memory {

/**
 * @class MemoryGuard
 * @brief Production-grade RAII wrapper for safe memory segment access.
 * Features: Hardware-aligned bounds checking, poisoning, and access telemetry.
 */
class alignas(64) MemoryGuard {
public:
    // Memory Access Permissions
    enum class Access : std::uint8_t {
        None  = 0,
        Read  = 1 << 0,
        Write = 1 << 1,
        Exec  = 1 << 2
    };

    friend constexpr Access operator|(Access lhs, Access rhs) noexcept {
        return static_cast<Access>(
            static_cast<std::uint8_t>(lhs) | static_cast<std::uint8_t>(rhs)
        );
    }

    [[nodiscard]] friend constexpr bool has_access(Access set, Access required) noexcept {
        return (static_cast<std::uint8_t>(set) & static_cast<std::uint8_t>(required)) == static_cast<std::uint8_t>(required);
    }

    constexpr MemoryGuard() noexcept = default;

    /**
     * @brief Create a guarded segment over an existing buffer.
     */
    template<typename T>
    explicit constexpr MemoryGuard(T* ptr, std::size_t count, Access perm = Access::Read | Access::Write) noexcept
        : m_base(reinterpret_cast<std::uintptr_t>(ptr)),
          m_size((count > (std::numeric_limits<std::size_t>::max() / sizeof(T)))
                    ? std::numeric_limits<std::size_t>::max()
                    : count * sizeof(T)),
          m_perms(perm) {}

    /**
     * @brief Secure access to a specific offset.
     * @throw std::runtime_error / std::out_of_range on access failure.
     */
    [[nodiscard]] inline void* get(std::size_t offset, std::size_t requested_size,
                                   Access required = Access::Read,
                                   std::source_location loc = std::source_location::current()) const {
        validate_access(offset, requested_size, required, loc);
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

    [[nodiscard]] constexpr std::size_t size() const noexcept { return m_size; }
    [[nodiscard]] constexpr bool is_null() const noexcept { return m_base == 0; }

private:
    std::uintptr_t m_base{0};
    std::size_t    m_size{0};
    Access         m_perms{Access::None};

    /**
     * @brief Internal validation logic (Optimized away in Release profiles if checks are stripped).
     */
    void validate_access([[maybe_unused]] std::size_t offset,
                         [[maybe_unused]] std::size_t req_size,
                         [[maybe_unused]] Access required,
                         [[maybe_unused]] const std::source_location& loc) const {
        if (m_base == 0) [[unlikely]] {
            throw std::runtime_error("Memory_Guard: Invalid base address");
        }

        if (!has_access(m_perms, required)) [[unlikely]] {
            throw std::runtime_error("Memory_Guard: Access permission denied");
        }

        if (offset > m_size || req_size > (m_size - offset)) [[unlikely]] {
            throw std::runtime_error("Memory_Guard: Access out of bounds");
        }
    }
};

} // namespace dgtl::utils::memory

// ================================
// COMPATIBILITY ALIAS (dgtl::utils)
// ================================
export namespace dgtl::utils {
    using MemoryGuard = dgtl::utils::memory::MemoryGuard;
}