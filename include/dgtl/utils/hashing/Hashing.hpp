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

#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string_view>
#include <type_traits>

namespace dgtl::utils::hashing {

/**
 * @class Hashing
 * @brief Cryptographic and non-cryptographic hash functions.
 * 
 * Provides multiple hash algorithms optimized for different use cases:
 * - FNV-1a: Fast, non-cryptographic, compile-time capable, good for hash tables
 * - Murmur3: Balanced speed and quality, good avalanche properties
 * - SipHash: More entropy, for security-sensitive contexts
 * - CRC32: Fast checksum for data integrity
 * - XXHash: Ultra-fast hash for non-security purposes
 */
class Hashing {
private:
    // ====================================================================
    // INTERNAL MIXING FUNCTIONS
    // ====================================================================

    [[nodiscard]] static constexpr uint64_t rotl64(uint64_t x, int r) noexcept {
        return std::rotl(x, r);
    }

    [[nodiscard]] static constexpr uint64_t fmix64(uint64_t h) noexcept {
        h ^= h >> 33;
        h *= 0xff51afd7ed558ccdULL;
        h ^= h >> 33;
        h *= 0xc4ceb9fe1a85ec53ULL;
        h ^= h >> 33;
        return h;
    }

    [[nodiscard]] static constexpr std::array<uint32_t, 256> build_crc32_table() noexcept {
        std::array<uint32_t, 256> table{};
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int j = 0; j < 8; ++j) {
                c = (c >> 1) ^ ((c & 1U) ? 0xedb88320U : 0U);
            }
            table[i] = c;
        }
        return table;
    }

    [[nodiscard]] static inline uint64_t read_u64(const void* ptr) noexcept {
        uint64_t v = 0;
        std::memcpy(&v, ptr, sizeof(v));
        return v;
    }

    [[nodiscard]] static inline uint32_t read_u32(const void* ptr) noexcept {
        uint32_t v = 0;
        std::memcpy(&v, ptr, sizeof(v));
        return v;
    }

    [[nodiscard]] static constexpr uint64_t xxh64_round(uint64_t acc, uint64_t input) noexcept {
        constexpr uint64_t PRIME64_2 = 14029467366897019727ULL;
        constexpr uint64_t PRIME64_1 = 11400714785074694791ULL;
        acc += input * PRIME64_2;
        acc = rotl64(acc, 31);
        acc *= PRIME64_1;
        return acc;
    }

    [[nodiscard]] static constexpr uint64_t xxh64_merge_round(uint64_t acc, uint64_t val) noexcept {
        constexpr uint64_t PRIME64_1 = 11400714785074694791ULL;
        constexpr uint64_t PRIME64_4 = 9650029242287828579ULL;
        acc ^= xxh64_round(0, val);
        acc = acc * PRIME64_1 + PRIME64_4;
        return acc;
    }

public:
    // ====================================================================
    // FNV-1a HASH (Fast Non-cryptographic, Compile-Time Capable)
    // ====================================================================

    static constexpr uint64_t FNV_PRIME = 1099511628211ULL;
    static constexpr uint64_t FNV_OFFSET_BASIS = 14695981039346656037ULL;
    static constexpr uint32_t FNV_PRIME_32 = 16777619U;
    static constexpr uint32_t FNV_OFFSET_BASIS_32 = 2166136261U;

    /**
     * @brief Compute FNV-1a hash (64-bit).
     */
    [[nodiscard]] static constexpr uint64_t fnv1a(const char* data, size_t len) noexcept {
        uint64_t hash = FNV_OFFSET_BASIS;
        for (size_t i = 0; i < len; ++i) {
            hash ^= static_cast<uint64_t>(static_cast<unsigned char>(data[i]));
            hash *= FNV_PRIME;
        }
        return hash;
    }

    /**
     * @brief Compute FNV-1a hash (32-bit).
     */
    [[nodiscard]] static constexpr uint32_t fnv1a_32(const char* data, size_t len) noexcept {
        uint32_t hash = FNV_OFFSET_BASIS_32;
        for (size_t i = 0; i < len; ++i) {
            hash ^= static_cast<uint32_t>(static_cast<unsigned char>(data[i]));
            hash *= FNV_PRIME_32;
        }
        return hash;
    }

    // ====================================================================
    // MURMUR3 HASH (Balanced Speed & Quality)
    // ====================================================================

    /**
     * @brief Compute Murmur3-64 hash with improved mixing.
     */
    [[nodiscard]] static inline uint64_t murmur3_64(const void* data, size_t len) noexcept {
        constexpr uint64_t c1 = 0x87c37b91114253d5ULL;
        constexpr uint64_t c2 = 0x4cf5ad432745937fULL;

        const uint8_t* bytes = static_cast<const uint8_t*>(data);
        uint64_t h1 = 0;
        uint64_t h2 = 0x9e3779b97f4a7c15ULL;

        const size_t nblocks = len / 16;
        for (size_t i = 0; i < nblocks; ++i) {
            uint64_t k1 = read_u64(bytes + i * 16);
            uint64_t k2 = read_u64(bytes + i * 16 + 8);

            k1 *= c1;
            k1 = rotl64(k1, 31);
            k1 *= c2;
            h1 ^= k1;
            h1 = rotl64(h1, 27);
            h1 += h2;
            h1 = h1 * 5 + 0x52dce729ULL;

            k2 *= c2;
            k2 = rotl64(k2, 33);
            k2 *= c1;
            h2 ^= k2;
            h2 = rotl64(h2, 31);
            h2 += h1;
            h2 = h2 * 5 + 0x38495ab5ULL;
        }

        const uint8_t* tail = bytes + nblocks * 16;
        uint64_t k1 = 0;
        uint64_t k2 = 0;

        switch (len & 15) {
            case 15: k2 ^= static_cast<uint64_t>(tail[14]) << 48; [[fallthrough]];
            case 14: k2 ^= static_cast<uint64_t>(tail[13]) << 40; [[fallthrough]];
            case 13: k2 ^= static_cast<uint64_t>(tail[12]) << 32; [[fallthrough]];
            case 12: k2 ^= static_cast<uint64_t>(tail[11]) << 24; [[fallthrough]];
            case 11: k2 ^= static_cast<uint64_t>(tail[10]) << 16; [[fallthrough]];
            case 10: k2 ^= static_cast<uint64_t>(tail[9]) << 8; [[fallthrough]];
            case 9:
                k2 ^= static_cast<uint64_t>(tail[8]);
                k2 *= c2;
                k2 = rotl64(k2, 33);
                k2 *= c1;
                h2 ^= k2;
                [[fallthrough]];
            case 8: k1 ^= static_cast<uint64_t>(tail[7]) << 56; [[fallthrough]];
            case 7: k1 ^= static_cast<uint64_t>(tail[6]) << 48; [[fallthrough]];
            case 6: k1 ^= static_cast<uint64_t>(tail[5]) << 40; [[fallthrough]];
            case 5: k1 ^= static_cast<uint64_t>(tail[4]) << 32; [[fallthrough]];
            case 4: k1 ^= static_cast<uint64_t>(tail[3]) << 24; [[fallthrough]];
            case 3: k1 ^= static_cast<uint64_t>(tail[2]) << 16; [[fallthrough]];
            case 2: k1 ^= static_cast<uint64_t>(tail[1]) << 8; [[fallthrough]];
            case 1:
                k1 ^= static_cast<uint64_t>(tail[0]);
                k1 *= c1;
                k1 = rotl64(k1, 31);
                k1 *= c2;
                h1 ^= k1;
                break;
            default:
                break;
        }

        h1 ^= static_cast<uint64_t>(len);
        h2 ^= static_cast<uint64_t>(len);

        h1 += h2;
        h2 += h1;

        h1 = fmix64(h1);
        h2 = fmix64(h2);

        h1 += h2;
        return h1;
    }

    // ====================================================================
    // SIPHASH-INSPIRED (Higher Entropy)
    // ====================================================================

    /**
     * @brief SipHash-like mixer: high-quality 64-bit combiner.
     */
    [[nodiscard]] static constexpr uint64_t siphash_mix(uint64_t seed, uint64_t value) noexcept {
        seed ^= value;
        seed = rotl64(seed, 13);
        seed ^= rotl64(seed, 17);
        return seed;
    }

    /**
     * @brief Combine two hash values with good mixing (Boost-style).
     */
    [[nodiscard]] static constexpr uint64_t combine(uint64_t seed, uint64_t value) noexcept {
        return seed ^ (value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2));
    }

    // ====================================================================
    // CRC32 (Data Integrity)
    // ====================================================================

    [[nodiscard]] static inline uint32_t crc32(const void* data, size_t len) noexcept {
        static constexpr auto kCrc32Table = build_crc32_table();
        const uint8_t* bytes = static_cast<const uint8_t*>(data);
        uint32_t crc = 0xffffffffU;

        for (size_t i = 0; i < len; ++i) {
            const uint8_t idx = static_cast<uint8_t>((crc ^ bytes[i]) & 0xffU);
            crc = (crc >> 8) ^ kCrc32Table[idx];
        }
        return crc ^ 0xffffffffU;
    }

    // ====================================================================
    // INTEGER HASHES (Splitmix64 variants for small values)
    // ====================================================================

    [[nodiscard]] static constexpr uint32_t hash32(uint32_t x) noexcept {
        x = ((x >> 16) ^ x) * 0x7feb352dU;
        x = ((x >> 16) ^ x) * 0x846ca68bU;
        return ((x >> 16) ^ x);
    }

    [[nodiscard]] static constexpr uint64_t hash64(uint64_t x) noexcept {
        x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
        x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
        return x ^ (x >> 31);
    }

    [[nodiscard]] static constexpr uint64_t hash_pair32(uint32_t a, uint32_t b) noexcept {
        uint64_t combined = (static_cast<uint64_t>(a) << 32) | b;
        return hash64(combined);
    }

    [[nodiscard]] static constexpr uint64_t hash_pair64(uint64_t a, uint64_t b) noexcept {
        uint64_t seed = hash64(a);
        return combine(seed, hash64(b));
    }

    // ====================================================================
    // XXHASH (Ultra-fast, non-cryptographic)
    // ====================================================================

    [[nodiscard]] static inline uint64_t xxhash64(const void* data, size_t len) noexcept {
        const uint8_t* bytes = static_cast<const uint8_t*>(data);
        const uint64_t PRIME64_1 = 11400714785074694791ULL;
        const uint64_t PRIME64_2 = 14029467366897019727ULL;
        const uint64_t PRIME64_3 = 1609587929392839161ULL;
        const uint64_t PRIME64_4 = 9650029242287828579ULL;
        const uint64_t PRIME64_5 = 2870177450012600261ULL;

        size_t offset = 0;
        uint64_t h64 = 0;

        if (len >= 32) {
            uint64_t v1 = PRIME64_1 + PRIME64_2;
            uint64_t v2 = PRIME64_2;
            uint64_t v3 = 0;
            uint64_t v4 = 0 - PRIME64_1;

            do {
                v1 = xxh64_round(v1, read_u64(bytes + offset));
                v2 = xxh64_round(v2, read_u64(bytes + offset + 8));
                v3 = xxh64_round(v3, read_u64(bytes + offset + 16));
                v4 = xxh64_round(v4, read_u64(bytes + offset + 24));
                offset += 32;
            } while (offset <= len - 32);

            h64 = rotl64(v1, 1) + rotl64(v2, 7) + rotl64(v3, 12) + rotl64(v4, 18);
            h64 = xxh64_merge_round(h64, v1);
            h64 = xxh64_merge_round(h64, v2);
            h64 = xxh64_merge_round(h64, v3);
            h64 = xxh64_merge_round(h64, v4);
        } else {
            h64 = PRIME64_5;
        }

        h64 += static_cast<uint64_t>(len);

        while (offset + 8 <= len) {
            const uint64_t k1 = xxh64_round(0, read_u64(bytes + offset));
            h64 ^= k1;
            h64 = rotl64(h64, 27) * PRIME64_1 + PRIME64_4;
            offset += 8;
        }

        if (offset + 4 <= len) {
            h64 ^= static_cast<uint64_t>(read_u32(bytes + offset)) * PRIME64_1;
            h64 = rotl64(h64, 23) * PRIME64_2 + PRIME64_3;
            offset += 4;
        }

        while (offset < len) {
            h64 ^= static_cast<uint64_t>(bytes[offset]) * PRIME64_5;
            h64 = rotl64(h64, 11) * PRIME64_1;
            ++offset;
        }

        h64 ^= h64 >> 33;
        h64 *= PRIME64_2;
        h64 ^= h64 >> 29;
        h64 *= PRIME64_3;
        h64 ^= h64 >> 32;
        return h64;
    }
};

// ====================================================================
// UNIVERSAL FAST HASH ROUTER
// ====================================================================

/**
 * @brief Routes arbitrary types to the fastest zero-allocation hash algorithm.
 */
template <typename T>
[[nodiscard]] constexpr uint64_t hash(const T& key) noexcept {
    using Decayed = std::remove_cv_t<std::remove_reference_t<T>>;

    if constexpr (std::is_same_v<Decayed, char*> || std::is_same_v<Decayed, const char*>) {
        if (key == nullptr) {
            return 0;
        }
        std::string_view sv{key};
        return Hashing::fnv1a(sv.data(), sv.size());
    } else if constexpr (std::is_constructible_v<std::string_view, T> && !std::is_integral_v<Decayed> && !std::is_pointer_v<Decayed>) {
        std::string_view sv{key};
        return Hashing::fnv1a(sv.data(), sv.size());
    } else if constexpr (std::is_integral_v<Decayed> || std::is_enum_v<Decayed>) {
        // Maps ints, bools, and enums to splitmix64
        if constexpr (std::is_same_v<Decayed, bool>) {
            return Hashing::hash64(key ? 1ULL : 0ULL);
        } else if constexpr (std::is_enum_v<Decayed>) {
            using Underlying = std::underlying_type_t<Decayed>;
            using CleanUnsigned = std::make_unsigned_t<Underlying>;
            return Hashing::hash64(static_cast<uint64_t>(static_cast<CleanUnsigned>(key)));
        } else {
            using CleanUnsigned = std::make_unsigned_t<Decayed>;
            return Hashing::hash64(static_cast<uint64_t>(static_cast<CleanUnsigned>(key)));
        }
    } else if constexpr (std::is_pointer_v<Decayed>) {
        // Maps raw pointers
        return Hashing::hash64(reinterpret_cast<uintptr_t>(key));
    } else if constexpr (requires { std::hash<Decayed>{}(key); }) {
        return Hashing::hash64(static_cast<uint64_t>(std::hash<Decayed>{}(key)));
    }
    return 0; // Unsupported type fallback
}

} // namespace dgtl::utils::hashing

namespace dgtl::utils {

using Hashing = dgtl::utils::hashing::Hashing;

template <typename T>
[[nodiscard]] constexpr uint64_t hash(const T& key) noexcept {
    return dgtl::utils::hashing::hash(key);
}

} // namespace dgtl::utils
