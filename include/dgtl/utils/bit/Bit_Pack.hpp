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

#include <cstdint>
#include <bit>
#include <limits>

namespace dgtl::utils::bit {

/**
 * @class BitPack
 * @brief Low-level bit manipulation utilities for compact IR encoding.
 * 
 * Provides extraction, packing, and bit manipulation operations used
 * throughout the IR layer for efficient storage and processing.
 * Upgraded to strict C++26 [[nodiscard]] constexpr for zero-overhead compilation.
 */
class BitPack {
public:
    [[nodiscard]] static constexpr uint64_t extract64(uint64_t value, uint32_t start, uint32_t length) noexcept {
        if (length == 0 || start >= 64) return 0;
        const uint32_t usable = (length > (64U - start)) ? (64U - start) : length;
        return (value >> start) & mask_low_bits(usable);
    }

    // ====================================================================
    // 64-BIT OPERATIONS
    // ====================================================================

    /**
     * @brief Extract a bitfield from a value.
     * @param value Source value
     * @param start Starting bit position
     * @param length Number of bits to extract
     * @return Extracted bitfield as uint32_t
     */
    [[nodiscard]] static constexpr uint32_t extract(uint64_t value, uint32_t start, uint32_t length) noexcept {
        return static_cast<uint32_t>(extract64(value, start, length));
    }

    /**
     * @brief Pack a bitfield into a value.
     * [ARCHITECT FIX]: Added 'length' parameter. The original implementation used 
     * 'bit' as the mask length, which caused data corruption at different offsets.
     * 
     * @param value Destination value (will be modified)
     * @param start Starting bit position
     * @param length Size of the bitfield being inserted
     * @param data Data to pack (will be masked to fit)
     * @return Modified value with bitfield inserted
     */
    [[nodiscard]] static constexpr uint64_t pack(uint64_t value, uint32_t start, uint32_t length, uint64_t data) noexcept {
        if (length == 0 || start >= 64) return value;

        const uint32_t usable = (length > (64U - start)) ? (64U - start) : length;
        if (usable == 0) return value;

        const uint64_t mask = mask_low_bits(usable);
        value &= ~(mask << start);       // Clear the target area
        value |= (data & mask) << start; // Insert the new data
        return value;
    }

    /**
     * @brief Legacy pack wrapper (Preserved to prevent breaking existing code).
     * Assumes the data being packed is a full 32-bit integer.
     */
    [[nodiscard]] static constexpr uint64_t pack(uint64_t value, uint32_t bit, uint32_t data) noexcept {
        return pack(value, bit, 32, static_cast<uint64_t>(data));
    }

    /**
     * @brief Count set bits (population count).
     */
    [[nodiscard]] static constexpr uint32_t popcount(uint64_t value) noexcept {
        return static_cast<uint32_t>(std::popcount(value));
    }

    /**
     * @brief Count leading zeros.
     */
    [[nodiscard]] static constexpr uint32_t clz(uint64_t value) noexcept {
        if (value == 0) return 64;
        return static_cast<uint32_t>(std::countl_zero(value));
    }

    /**
     * @brief Count trailing zeros.
     */
    [[nodiscard]] static constexpr uint32_t ctz(uint64_t value) noexcept {
        if (value == 0) return 64;
        return static_cast<uint32_t>(std::countr_zero(value));
    }

    /**
     * @brief Check if a single bit is set.
     */
    [[nodiscard]] static constexpr bool is_set(uint64_t value, uint32_t bit) noexcept {
        if (bit >= 64) return false;
        return (value & (1ULL << bit)) != 0;
    }

    /**
     * @brief Set a single bit to 1.
     */
    [[nodiscard]] static constexpr uint64_t set_bit(uint64_t value, uint32_t bit) noexcept {
        if (bit >= 64) return value; // Prevent UB
        return value | (1ULL << bit);
    }

    /**
     * @brief Clear a single bit to 0.
     */
    [[nodiscard]] static constexpr uint64_t clear_bit(uint64_t value, uint32_t bit) noexcept {
        if (bit >= 64) return value; // Prevent UB
        return value & ~(1ULL << bit);
    }

    /**
     * @brief Toggle a single bit.
     */
    [[nodiscard]] static constexpr uint64_t toggle_bit(uint64_t value, uint32_t bit) noexcept {
        if (bit >= 64) return value; // Prevent UB
        return value ^ (1ULL << bit);
    }

    /**
     * @brief Rotate left by N bits.
     * [ARCHITECT FIX]: Delegates to std::rotl to prevent Undefined Behavior when n=0.
     */
    [[nodiscard]] static constexpr uint64_t rotl(uint64_t value, uint32_t n) noexcept {
        return std::rotl(value, static_cast<int>(n & 63));
    }

    /**
     * @brief Rotate right by N bits.
     * [ARCHITECT FIX]: Delegates to std::rotr to prevent Undefined Behavior when n=0.
     */
    [[nodiscard]] static constexpr uint64_t rotr(uint64_t value, uint32_t n) noexcept {
        return std::rotr(value, static_cast<int>(n & 63));
    }

    // ====================================================================
    // 32-BIT OPERATIONS (For performance-critical 32-bit code)
    // ====================================================================

    [[nodiscard]] static constexpr uint32_t popcount32(uint32_t value) noexcept {
        return static_cast<uint32_t>(std::popcount(value));
    }

    [[nodiscard]] static constexpr uint32_t clz32(uint32_t value) noexcept {
        if (value == 0) return 32;
        return static_cast<uint32_t>(std::countl_zero(value));
    }

    [[nodiscard]] static constexpr uint32_t ctz32(uint32_t value) noexcept {
        if (value == 0) return 32;
        return static_cast<uint32_t>(std::countr_zero(value));
    }

    [[nodiscard]] static constexpr uint32_t rotl32(uint32_t value, uint32_t n) noexcept {
        return std::rotl(value, static_cast<int>(n & 31));
    }

    [[nodiscard]] static constexpr uint32_t rotr32(uint32_t value, uint32_t n) noexcept {
        return std::rotr(value, static_cast<int>(n & 31));
    }

    // ====================================================================
    // BYTE OPERATIONS
    // ====================================================================

    [[nodiscard]] static constexpr uint64_t byteswap64(uint64_t value) noexcept {
        return std::byteswap(value);
    }

    [[nodiscard]] static constexpr uint32_t byteswap32(uint32_t value) noexcept {
        return std::byteswap(value);
    }

    [[nodiscard]] static constexpr uint16_t byteswap16(uint16_t value) noexcept {
        return std::byteswap(value);
    }

    // ====================================================================
    // BIT RANGE OPERATIONS
    // ====================================================================

    /**
     * @brief Extract lower N bits.
     */
    [[nodiscard]] static constexpr uint64_t mask_low_bits(uint32_t n) noexcept {
        if (n == 0) return 0;
        if (n >= 64) return ~0ULL;
        return (1ULL << n) - 1;
    }

    /**
     * @brief Create a mask with bits [start, start+length) set.
     */
    [[nodiscard]] static constexpr uint64_t range_mask(uint32_t start, uint32_t length) noexcept {
        if (length == 0 || start >= 64) return 0;
        const uint32_t usable = (length > (64U - start)) ? (64U - start) : length;
        if (usable == 64) return ~0ULL;
        return ((1ULL << usable) - 1ULL) << start;
    }

    /**
     * @brief Check if power of two.
     * [ARCHITECT FIX]: Uses C++20 hardware mapped primitive std::has_single_bit.
     */
    [[nodiscard]] static constexpr bool is_power_of_two(uint64_t value) noexcept {
        return std::has_single_bit(value); 
    }

    /**
     * @brief Find next power of two (>= value).
     * [ARCHITECT FIX]: Uses C++20 hardware mapped primitive std::bit_ceil.
     */
    [[nodiscard]] static constexpr uint64_t next_power_of_two(uint64_t value) noexcept {
        if (value <= 1) return 1;
        if (value > (1ULL << 63)) return 0; // Overflow sentinel, mirrors std::bit_ceil overflow behavior.
        return std::bit_ceil(value);
    }
};

} // namespace dgtl::utils::bit

namespace dgtl::utils {

using BitPack = dgtl::utils::bit::BitPack;

} // namespace dgtl::utils