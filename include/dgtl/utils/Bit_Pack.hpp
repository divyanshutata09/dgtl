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

namespace dgtl {
namespace utils {

/**
 * @class BitPack
 * @brief Low-level bit manipulation utilities for compact IR encoding.
 * 
 * Provides extraction, packing, and bit manipulation operations used
 * throughout the IR layer for efficient storage and processing.
 * All operations are constexpr-compatible for compile-time computation.
 */
class BitPack {
public:
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
    static inline uint32_t extract(uint64_t value, uint32_t start, uint32_t length) noexcept {
        if (length >= 64) return static_cast<uint32_t>(value);
        uint64_t mask = (1ULL << length) - 1;
        return static_cast<uint32_t>((value >> start) & mask);
    }

    /**
     * @brief Pack a bitfield into a value.
     * @param value Destination value (will be modified)
     * @param bit Starting bit position
     * @param data Data to pack (will be masked to fit)
     * @return Modified value with bitfield inserted
     */
    static inline uint64_t pack(uint64_t value, uint32_t bit, uint32_t data) noexcept {
        uint64_t mask = (1ULL << bit) - 1;
        value &= ~(mask << bit);
        value |= (static_cast<uint64_t>(data) & mask) << bit;
        return value;
    }

    /**
     * @brief Count set bits (population count).
     * @param value Input value
     * @return Number of bits set to 1
     */
    static inline uint32_t popcount(uint64_t value) noexcept {
        return static_cast<uint32_t>(std::popcount(value));
    }

    /**
     * @brief Count leading zeros.
     * @param value Input value (0 returns 64)
     * @return Number of leading zero bits
     */
    static inline uint32_t clz(uint64_t value) noexcept {
        if (value == 0) return 64;
        return std::countl_zero(value);
    }

    /**
     * @brief Count trailing zeros.
     * @param value Input value (0 returns 64)
     * @return Number of trailing zero bits
     */
    static inline uint32_t ctz(uint64_t value) noexcept {
        if (value == 0) return 64;
        return std::countr_zero(value);
    }

    /**
     * @brief Check if a single bit is set.
     * @param value Input value
     * @param bit Bit position to check
     * @return true if bit is set
     */
    static inline bool is_set(uint64_t value, uint32_t bit) noexcept {
        return (value & (1ULL << bit)) != 0;
    }

    /**
     * @brief Set a single bit to 1.
     * @param value Input value (will be modified)
     * @param bit Bit position
     * @return Value with bit set
     */
    static inline uint64_t set_bit(uint64_t value, uint32_t bit) noexcept {
        return value | (1ULL << bit);
    }

    /**
     * @brief Clear a single bit to 0.
     * @param value Input value (will be modified)
     * @param bit Bit position
     * @return Value with bit cleared
     */
    static inline uint64_t clear_bit(uint64_t value, uint32_t bit) noexcept {
        return value & ~(1ULL << bit);
    }

    /**
     * @brief Toggle a single bit.
     * @param value Input value
     * @param bit Bit position
     * @return Value with bit toggled
     */
    static inline uint64_t toggle_bit(uint64_t value, uint32_t bit) noexcept {
        return value ^ (1ULL << bit);
    }

    /**
     * @brief Rotate left by N bits.
     * @param value Input value
     * @param n Rotation amount
     * @return Rotated value
     */
    static inline uint64_t rotl(uint64_t value, uint32_t n) noexcept {
        n &= 63;
        return (value << n) | (value >> (64 - n));
    }

    /**
     * @brief Rotate right by N bits.
     * @param value Input value
     * @param n Rotation amount
     * @return Rotated value
     */
    static inline uint64_t rotr(uint64_t value, uint32_t n) noexcept {
        n &= 63;
        return (value >> n) | (value << (64 - n));
    }

    // ====================================================================
    // 32-BIT OPERATIONS (For performance-critical 32-bit code)
    // ====================================================================

    /**
     * @brief Count set bits in 32-bit value.
     */
    static inline uint32_t popcount32(uint32_t value) noexcept {
        return static_cast<uint32_t>(std::popcount(value));
    }

    /**
     * @brief Count leading zeros in 32-bit value.
     */
    static inline uint32_t clz32(uint32_t value) noexcept {
        if (value == 0) return 32;
        return std::countl_zero(value);
    }

    /**
     * @brief Count trailing zeros in 32-bit value.
     */
    static inline uint32_t ctz32(uint32_t value) noexcept {
        if (value == 0) return 32;
        return std::countr_zero(value);
    }

    /**
     * @brief Rotate left in 32-bit value.
     */
    static inline uint32_t rotl32(uint32_t value, uint32_t n) noexcept {
        n &= 31;
        return (value << n) | (value >> (32 - n));
    }

    /**
     * @brief Rotate right in 32-bit value.
     */
    static inline uint32_t rotr32(uint32_t value, uint32_t n) noexcept {
        n &= 31;
        return (value >> n) | (value << (32 - n));
    }

    // ====================================================================
    // BYTE OPERATIONS
    // ====================================================================

    /**
     * @brief Reverse bytes in 64-bit value (endian swap).
     */
    static inline uint64_t byteswap64(uint64_t value) noexcept {
        return std::byteswap(value);
    }

    /**
     * @brief Reverse bytes in 32-bit value (endian swap).
     */
    static inline uint32_t byteswap32(uint32_t value) noexcept {
        return std::byteswap(value);
    }

    /**
     * @brief Reverse bytes in 16-bit value (endian swap).
     */
    static inline uint16_t byteswap16(uint16_t value) noexcept {
        return std::byteswap(value);
    }

    // ====================================================================
    // BIT RANGE OPERATIONS
    // ====================================================================

    /**
     * @brief Extract lower N bits.
     */
    static inline uint64_t mask_low_bits(uint32_t n) noexcept {
        if (n >= 64) return ~0ULL;
        return (1ULL << n) - 1;
    }

    /**
     * @brief Create a mask with bits [start, start+length) set.
     */
    static inline uint64_t range_mask(uint32_t start, uint32_t length) noexcept {
        if (length >= 64) return ~0ULL;
        return ((1ULL << length) - 1) << start;
    }

    /**
     * @brief Check if power of two.
     */
    static inline bool is_power_of_two(uint64_t value) noexcept {
        return value != 0 && (value & (value - 1)) == 0;
    }

    /**
     * @brief Find next power of two (>= value).
     */
    static inline uint64_t next_power_of_two(uint64_t value) noexcept {
        if (value == 0) return 1;
        value--;
        value |= value >> 1;
        value |= value >> 2;
        value |= value >> 4;
        value |= value >> 8;
        value |= value >> 16;
        value |= value >> 32;
        return value + 1;
    }
};

} // namespace utils
} // namespace dgtl
