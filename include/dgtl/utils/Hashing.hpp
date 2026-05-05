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
#include <cstring>

namespace dgtl {
namespace utils {

/**
 * @class Hashing
 * @brief Cryptographic and non-cryptographic hash functions.
 * 
 * Provides multiple hash algorithms optimized for different use cases:
 * - FNV-1a: Fast, non-cryptographic, good for hash tables
 * - Murmur3: Balanced speed and quality, good avalanche properties
 * - SipHash: More entropy, for security-sensitive contexts
 * - CRC32: Fast checksum for data integrity
 * - XXHash: Ultra-fast hash for non-security purposes
 */
class Hashing {
public:
    // ====================================================================
    // FNV-1a HASH (Fast Non-cryptographic)
    // ====================================================================

    static constexpr uint64_t FNV_PRIME = 1099511628211ULL;
    static constexpr uint64_t FNV_OFFSET_BASIS = 14695981039346656037ULL;
    static constexpr uint32_t FNV_PRIME_32 = 16777619U;
    static constexpr uint32_t FNV_OFFSET_BASIS_32 = 2166136261U;

    /**
     * @brief Compute FNV-1a hash (64-bit).
     * @param data Pointer to data to hash
     * @param len Length in bytes
     * @return 64-bit hash value
     */
    static inline uint64_t fnv1a(const void* data, size_t len) noexcept {
        uint64_t hash = FNV_OFFSET_BASIS;
        const uint8_t* bytes = static_cast<const uint8_t*>(data);
        for (size_t i = 0; i < len; ++i) {
            hash ^= bytes[i];
            hash *= FNV_PRIME;
        }
        return hash;
    }

    /**
     * @brief Compute FNV-1a hash (32-bit).
     */
    static inline uint32_t fnv1a_32(const void* data, size_t len) noexcept {
        uint32_t hash = FNV_OFFSET_BASIS_32;
        const uint8_t* bytes = static_cast<const uint8_t*>(data);
        for (size_t i = 0; i < len; ++i) {
            hash ^= bytes[i];
            hash *= FNV_PRIME_32;
        }
        return hash;
    }

    // ====================================================================
    // MURMUR3 HASH (Balanced Speed & Quality)
    // ====================================================================

    /**
     * @brief Compute Murmur3-64 hash with improved mixing.
     * @param data Pointer to data to hash
     * @param len Length in bytes
     * @return 64-bit hash value
     */
    static inline uint64_t murmur3_64(const void* data, size_t len) noexcept {
        const uint8_t* bytes = static_cast<const uint8_t*>(data);
        const uint64_t c1 = UINT64_C(0x87c4e5910d858f31);
        const uint64_t c2 = UINT64_C(0xaf550642d3fa2fec);
        
        uint64_t h = 0;
        
        // Process 8-byte blocks
        size_t nblocks = len / 8;
        for (size_t i = 0; i < nblocks; ++i) {
            uint64_t v;
            std::memcpy(&v, bytes + i * 8, 8);
            
            v *= c1;
            v = rotl64(v, 31);
            v *= c2;
            
            h ^= v;
            h = rotl64(h, 27);
            h = h * 5 + 0x52dce729U;
        }
        
        // Process tail (remaining bytes)
        const uint8_t* tail = bytes + nblocks * 8;
        uint64_t v = 0;
        switch (len & 7) {
            case 7: v |= static_cast<uint64_t>(tail[6]) << 48;
                    [[fallthrough]];
            case 6: v |= static_cast<uint64_t>(tail[5]) << 40;
                    [[fallthrough]];
            case 5: v |= static_cast<uint64_t>(tail[4]) << 32;
                    [[fallthrough]];
            case 4: v |= static_cast<uint64_t>(tail[3]) << 24;
                    [[fallthrough]];
            case 3: v |= static_cast<uint64_t>(tail[2]) << 16;
                    [[fallthrough]];
            case 2: v |= static_cast<uint64_t>(tail[1]) << 8;
                    [[fallthrough]];
            case 1: v |= static_cast<uint64_t>(tail[0]);
                    v *= c1;
                    v = rotl64(v, 31);
                    v *= c2;
                    h ^= v;
        }
        
        // Finalization mixing
        h ^= len;
        h = fmix64(h);
        return h;
    }

    // ====================================================================
    // SIPHASH-INSPIRED (Higher Entropy)
    // ====================================================================

    /**
     * @brief SipHash-like mixer: high-quality 64-bit combiner.
     * @param seed Seed value
     * @param value Value to mix in
     * @return Mixed hash
     */
    static inline uint64_t siphash_mix(uint64_t seed, uint64_t value) noexcept {
        seed ^= value;
        // Simulate SipHash mixing: rotate and XOR
        seed = rotl64(seed, 13);
        seed ^= rotl64(seed, 17);
        return seed;
    }

    /**
     * @brief Combine two hash values with good mixing (Boost-style).
     * @param seed Initial seed
     * @param value Value to incorporate
     * @return Combined hash
     */
    static inline uint64_t combine(uint64_t seed, uint64_t value) noexcept {
        // Golden ratio mixing
        return seed ^ (value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2));
    }

    // ====================================================================
    // CRC32 (Data Integrity)
    // ====================================================================

    /**
     * @brief Compute CRC32 hash.
     */
    static inline uint32_t crc32(const void* data, size_t len) noexcept {
        const uint8_t* bytes = static_cast<const uint8_t*>(data);
        uint32_t crc = 0xffffffffU;
        
        for (size_t i = 0; i < len; ++i) {
            crc ^= bytes[i];
            for (int j = 0; j < 8; ++j) {
                crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320U : 0);
            }
        }
        
        return crc ^ 0xffffffffU;
    }

    // ====================================================================
    // INTEGER HASHES (For small values)
    // ====================================================================

    /**
     * @brief Fast 32-bit hash for small integers.
     * @param x Value to hash
     * @return 32-bit hash
     */
    static inline uint32_t hash32(uint32_t x) noexcept {
        x = ((x >> 16) ^ x) * 0x7feb352dU;
        x = ((x >> 16) ^ x) * 0x846ca68bU;
        return ((x >> 16) ^ x);
    }

    /**
     * @brief Fast 64-bit hash for small integers.
     * @param x Value to hash
     * @return 64-bit hash
     */
    static inline uint64_t hash64(uint64_t x) noexcept {
        x = (x ^ (x >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
        x = (x ^ (x >> 27)) * UINT64_C(0x94d049bb133111eb);
        return x ^ (x >> 31);
    }

    /**
     * @brief Hash two 32-bit values together.
     */
    static inline uint64_t hash_pair32(uint32_t a, uint32_t b) noexcept {
        uint64_t combined = (static_cast<uint64_t>(a) << 32) | b;
        return hash64(combined);
    }

    /**
     * @brief Hash two 64-bit values together.
     */
    static inline uint64_t hash_pair64(uint64_t a, uint64_t b) noexcept {
        uint64_t seed = hash64(a);
        return combine(seed, hash64(b));
    }

    // ====================================================================
    // XXHASH (Ultra-fast, non-cryptographic)
    // ====================================================================

    /**
     * @brief XXHash64-inspired ultra-fast hash.
     */
    static inline uint64_t xxhash64(const void* data, size_t len) noexcept {
        const uint8_t* bytes = static_cast<const uint8_t*>(data);
        const uint64_t PRIME64_1 = UINT64_C(11400714785074694791);
        const uint64_t PRIME64_2 = UINT64_C(14029467366386228477);
        const uint64_t PRIME64_3 = UINT64_C(1609587929392839161);
        const uint64_t PRIME64_4 = UINT64_C(9650029242287828579);
        const uint64_t PRIME64_5 = UINT64_C(2870177450012600261);
        
        uint64_t h64 = PRIME64_5 + len;
        size_t offset = 0;
        
        // Process 32-byte blocks
        while (offset + 32 <= len) {
            uint64_t v1, v2, v3, v4;
            std::memcpy(&v1, bytes + offset, 8);
            std::memcpy(&v2, bytes + offset + 8, 8);
            std::memcpy(&v3, bytes + offset + 16, 8);
            std::memcpy(&v4, bytes + offset + 24, 8);
            
            h64 ^= (v1 * PRIME64_2) + (v2 * PRIME64_3) + (v3 * PRIME64_4) + (v4 * PRIME64_1);
            offset += 32;
        }
        
        // Process 8-byte blocks
        while (offset + 8 <= len) {
            uint64_t v;
            std::memcpy(&v, bytes + offset, 8);
            h64 ^= v * PRIME64_1;
            offset += 8;
        }
        
        // Process remaining bytes
        while (offset < len) {
            h64 ^= bytes[offset] * PRIME64_5;
            offset++;
        }
        
        return fmix64(h64);
    }

private:
    // ====================================================================
    // INTERNAL MIXING FUNCTIONS
    // ====================================================================

    static inline uint64_t rotl64(uint64_t x, int r) noexcept {
        return (x << r) | (x >> (64 - r));
    }

    static inline uint64_t fmix64(uint64_t h) noexcept {
        h ^= h >> 33;
        h *= UINT64_C(0xff51afd7ed558ccd);
        h ^= h >> 33;
        return h;
    }
};

} // namespace utils
} // namespace dgtl
