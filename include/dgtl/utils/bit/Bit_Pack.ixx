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

// Global Module Fragment: Include core bitwise STL infrastructure
#include <cstdint>
#include <bit>
#include <limits>

export module dgtl.bit_pack;

export namespace dgtl::utils::bit {

/**
 * @class BitPack
 * @brief Low-level bit manipulation utilities for compact IR encoding.
 * Upgraded to strict C++26 [[nodiscard]] constexpr for zero-overhead compilation.
 */
class BitPack {
public:
    // ====================================================================
    // BIT RANGE OPERATIONS & MASKS (Moved up to prevent forward-decl bugs)
    // ====================================================================

    /**
     * @brief Extract lower N bits.
     */
    [[nodiscard]] static constexpr std::uint64_t mask_low_bits(std::uint32_t n) noexcept {
        if (n == 0) return 0;
        if (n >= 64) return ~0ULL;
        return (1ULL << n) - 1;
    }

    /**
     * @brief Create a mask with bits [start, start+length) set.
     */
    [[nodiscard]] static constexpr std::uint64_t range_mask(std::uint32_t start, std::uint32_t length) noexcept {
        if (length == 0 || start >= 64) return 0;
        const std::uint32_t usable = (length > (64U - start)) ? (64U - start) : length;
        if (usable == 64) return ~0ULL;
        return ((1ULL << usable) - 1ULL) << start;
    }

    // ====================================================================
    // 64-BIT EXTRACTION & PACKING
    // ====================================================================

    [[nodiscard]] static constexpr std::uint64_t extract64(std::uint64_t value, std::uint32_t start, std::uint32_t length) noexcept {
        if (length == 0 || start >= 64) return 0;
        const std::uint32_t usable = (length > (64U - start)) ? (64U - start) : length;
        return (value >> start) & mask_low_bits(usable);
    }

    /**
     * @brief Extract a bitfield from a value.
     */
    [[nodiscard]] static constexpr std::uint32_t extract(std::uint64_t value, std::uint32_t start, std::uint32_t length) noexcept {
        return static_cast<std::uint32_t>(extract64(value, start, length));
    }

    /**
     * @brief Pack a bitfield into a value.
     */
    [[nodiscard]] static constexpr std::uint64_t pack(std::uint64_t value, std::uint32_t start, std::uint32_t length, std::uint64_t data) noexcept {
        if (length == 0 || start >= 64) return value;

        const std::uint32_t usable = (length > (64U - start)) ? (64U - start) : length;
        if (usable == 0) return value;

        const std::uint64_t mask = mask_low_bits(usable);
        value &= ~(mask << start);       // Clear target area
        value |= (data & mask) << start; // Insert masked data
        return value;
    }

    /**
     * @brief Legacy pack wrapper assuming 32-bit width.
     */
    [[nodiscard]] static constexpr std::uint64_t pack(std::uint64_t value, std::uint32_t bit, std::uint32_t data) noexcept {
        return pack(value, bit, 32, static_cast<std::uint64_t>(data));
    }

    // ====================================================================
    // CORE BIT MANIPULATION (Hardware Mapped Primitives)
    // ====================================================================

    [[nodiscard]] static constexpr std::uint32_t popcount(std::uint64_t value) noexcept {
        return static_cast<std::uint32_t>(std::popcount(value));
    }

    [[nodiscard]] static constexpr std::uint32_t clz(std::uint64_t value) noexcept {
        if (value == 0) return 64;
        return static_cast<std::uint32_t>(std::countl_zero(value));
    }

    [[nodiscard]] static constexpr std::uint32_t ctz(std::uint64_t value) noexcept {
        if (value == 0) return 64;
        return static_cast<std::uint32_t>(std::countr_zero(value));
    }

    [[nodiscard]] static constexpr bool is_set(std::uint64_t value, std::uint32_t bit) noexcept {
        if (bit >= 64) return false;
        return (value & (1ULL << bit)) != 0;
    }

    [[nodiscard]] static constexpr std::uint64_t set_bit(std::uint64_t value, std::uint32_t bit) noexcept {
        if (bit >= 64) return value; 
        return value | (1ULL << bit);
    }

    [[nodiscard]] static constexpr std::uint64_t clear_bit(std::uint64_t value, std::uint32_t bit) noexcept {
        if (bit >= 64) return value; 
        return value & ~(1ULL << bit);
    }

    [[nodiscard]] static constexpr std::uint64_t toggle_bit(std::uint64_t value, std::uint32_t bit) noexcept {
        if (bit >= 64) return value; 
        return value ^ (1ULL << bit);
    }

    [[nodiscard]] static constexpr std::uint64_t rotl(std::uint64_t value, std::uint32_t n) noexcept {
        return std::rotl(value, static_cast<int>(n));
    }

    [[nodiscard]] static constexpr std::uint64_t rotr(std::uint64_t value, std::uint32_t n) noexcept {
        return std::rotr(value, static_cast<int>(n));
    }

    // ====================================================================
    // 32-BIT OPERATIONS
    // ====================================================================

    [[nodiscard]] static constexpr std::uint32_t popcount32(std::uint32_t value) noexcept {
        return static_cast<std::uint32_t>(std::popcount(value));
    }

    [[nodiscard]] static constexpr std::uint32_t clz32(std::uint32_t value) noexcept {
        if (value == 0) return 32;
        return static_cast<std::uint32_t>(std::countl_zero(value));
    }

    [[nodiscard]] static constexpr std::uint32_t ctz32(std::uint32_t value) noexcept {
        if (value == 0) return 32;
        return static_cast<std::uint32_t>(std::countr_zero(value));
    }

    [[nodiscard]] static constexpr std::uint32_t rotl32(std::uint32_t value, std::uint32_t n) noexcept {
        return std::rotl(value, static_cast<int>(n));
    }

    [[nodiscard]] static constexpr std::uint32_t rotr32(std::uint32_t value, std::uint32_t n) noexcept {
        return std::rotr(value, static_cast<int>(n));
    }

    // ====================================================================
    // ENDIAN END CODING / BYTESWAPS
    // ====================================================================

    [[nodiscard]] static constexpr std::uint64_t byteswap64(std::uint64_t value) noexcept {
        return std::byteswap(value);
    }

    [[nodiscard]] static constexpr std::uint32_t byteswap32(std::uint32_t value) noexcept {
        return std::byteswap(value);
    }

    [[nodiscard]] static constexpr std::uint16_t byteswap16(std::uint16_t value) noexcept {
        return std::byteswap(value);
    }

    // ====================================================================
    // MATHEMATICAL MATRICES / ALIGNMENTS
    // ====================================================================

    [[nodiscard]] static constexpr bool is_power_of_two(std::uint64_t value) noexcept {
        return std::has_single_bit(value); 
    }

    [[nodiscard]] static constexpr std::uint64_t next_power_of_two(std::uint64_t value) noexcept {
        if (value <= 1) return 1;
        if (value > (1ULL << 63)) return 0; // Overflow tracking bounds
        return std::bit_ceil(value);
    }
};

} // namespace dgtl::utils::bit

// ================================
// COMPATIBILITY ALIAS (dgtl::utils)
// ================================
export namespace dgtl::utils {
    using BitPack = dgtl::utils::bit::BitPack;
} // namespace dgtl::utils