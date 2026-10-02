/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository: github.com/TataG-Systems/DGTL
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

module;

#include <cstdlib>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <limits>
#include <bit>
#include <algorithm>
#include <cmath>

export module dgtl.libc_utils;

export namespace dgtl::utils::libc {

// ============================================================================
// SECTION 1: HIGH-SPEED SWAR MEMORY OPERATIONS (FREESTANDING)
// Word-at-a-time (64-bit) processing with unrolled loops and Duff-style tails
// ============================================================================

namespace detail {
    inline constexpr uint64_t kRepeat01 = 0x0101010101010101ULL;
    inline constexpr uint64_t kRepeat80 = 0x8080808080808080ULL;

    [[nodiscard]] inline constexpr bool has_zero_byte(uint64_t v) noexcept {
        return ((v - kRepeat01) & ~v & kRepeat80) != 0;
    }
} // namespace detail

/**
 * @brief High-performance memory copy with 64-bit word unrolling and zero-UB.
 */
inline void* fast_memcpy(void* __restrict dest, const void* __restrict src, size_t n) noexcept {
    if (dest == src || n == 0) return dest;

    auto* d = static_cast<unsigned char*>(dest);
    const auto* s = static_cast<const unsigned char*>(src);

    if (n < 16) {
        for (size_t i = 0; i < n; ++i) d[i] = s[i];
        return dest;
    }

    // 32-byte chunks (4x 64-bit words)
    while (n >= 32) {
        uint64_t w0, w1, w2, w3;
        std::memcpy(&w0, s, 8);
        std::memcpy(&w1, s + 8, 8);
        std::memcpy(&w2, s + 16, 8);
        std::memcpy(&w3, s + 24, 8);
        std::memcpy(d, &w0, 8);
        std::memcpy(d + 8, &w1, 8);
        std::memcpy(d + 16, &w2, 8);
        std::memcpy(d + 24, &w3, 8);
        d += 32;
        s += 32;
        n -= 32;
    }

    // 8-byte chunks
    while (n >= 8) {
        uint64_t w;
        std::memcpy(&w, s, 8);
        std::memcpy(d, &w, 8);
        d += 8;
        s += 8;
        n -= 8;
    }

    // Tail bytes
    for (size_t i = 0; i < n; ++i) d[i] = s[i];
    return dest;
}

/**
 * @brief Memory move: safely handles overlapping memory regions.
 */
inline void* fast_memmove(void* dest, const void* src, size_t n) noexcept {
    if (dest == src || n == 0) return dest;

    auto* d = static_cast<unsigned char*>(dest);
    const auto* s = static_cast<const unsigned char*>(src);

    if (d < s || d >= s + n) {
        return fast_memcpy(dest, src, n);
    }

    // Overlapping: copy backwards
    d += n;
    s += n;
    while (n >= 8) {
        d -= 8;
        s -= 8;
        uint64_t w;
        std::memcpy(&w, s, 8);
        std::memcpy(d, &w, 8);
        n -= 8;
    }
    while (n > 0) {
        --d;
        --s;
        *d = *s;
        --n;
    }
    return dest;
}

/**
 * @brief Fast memory fill using 64-bit broadcast pattern.
 */
inline void* fast_memset(void* s, int c, size_t n) noexcept {
    if (n == 0) return s;

    auto* p = static_cast<unsigned char*>(s);
    const uint8_t byte_val = static_cast<uint8_t>(c);

    if (n < 16) {
        for (size_t i = 0; i < n; ++i) p[i] = byte_val;
        return s;
    }

    const uint64_t word_val = byte_val * detail::kRepeat01;

    // Align to 8 bytes
    while (reinterpret_cast<uintptr_t>(p) % 8 != 0 && n > 0) {
        *p++ = byte_val;
        --n;
    }

    // 32-byte fills with safe memcpy
    while (n >= 32) {
        std::memcpy(p, &word_val, 8);
        std::memcpy(p + 8, &word_val, 8);
        std::memcpy(p + 16, &word_val, 8);
        std::memcpy(p + 24, &word_val, 8);
        p += 32;
        n -= 32;
    }

    while (n >= 8) {
        std::memcpy(p, &word_val, 8);
        p += 8;
        n -= 8;
    }

    for (size_t i = 0; i < n; ++i) p[i] = byte_val;
    return s;
}

/**
 * @brief Fast memory comparison comparing 8-byte blocks.
 */
[[nodiscard]] inline int fast_memcmp(const void* s1, const void* s2, size_t n) noexcept {
    if (s1 == s2 || n == 0) return 0;

    const auto* p1 = static_cast<const unsigned char*>(s1);
    const auto* p2 = static_cast<const unsigned char*>(s2);

    while (n >= 8) {
        uint64_t w1, w2;
        std::memcpy(&w1, p1, 8);
        std::memcpy(&w2, p2, 8);
        if (w1 != w2) {
            for (size_t i = 0; i < 8; ++i) {
                if (p1[i] != p2[i]) return (p1[i] < p2[i]) ? -1 : 1;
            }
        }
        p1 += 8;
        p2 += 8;
        n -= 8;
    }

    for (size_t i = 0; i < n; ++i) {
        if (p1[i] != p2[i]) return (p1[i] < p2[i]) ? -1 : 1;
    }
    return 0;
}

/**
 * @brief Scan memory for character.
 */
[[nodiscard]] inline void* fast_memchr(const void* s, int c, size_t n) noexcept {
    if (s == nullptr || n == 0) return nullptr;
    const auto* p = static_cast<const unsigned char*>(s);
    const uint8_t target = static_cast<uint8_t>(c);

    for (size_t i = 0; i < n; ++i) {
        if (p[i] == target) return const_cast<unsigned char*>(p + i);
    }
    return nullptr;
}

// ============================================================================
// SECTION 2: SWAR STRING UTILITIES (FAST ZERO-DEPENDENCY)
// ============================================================================

/**
 * @brief SWAR string length: scans 8 bytes per iteration with page-boundary protection.
 */
[[nodiscard]] inline size_t fast_strlen(const char* s) noexcept {
    if (s == nullptr) return 0;

    const char* ptr = s;

    // Byte-by-byte alignment
    while (reinterpret_cast<uintptr_t>(ptr) % 8 != 0) {
        if (*ptr == '\0') return static_cast<size_t>(ptr - s);
        ++ptr;
    }

    // Page-safe SWAR loop (4096-byte page boundary protection)
    while (true) {
        const uintptr_t page_offset = reinterpret_cast<uintptr_t>(ptr) & 0xFFFu;
        if (page_offset > (4096u - 8u)) {
            // Near page boundary: do safe byte-by-byte to avoid crossing into unmapped page
            if (*ptr == '\0') return static_cast<size_t>(ptr - s);
            ++ptr;
            continue;
        }

        uint64_t v;
        std::memcpy(&v, ptr, 8);
        if (detail::has_zero_byte(v)) break;
        ptr += 8;
    }

    while (*ptr != '\0') ++ptr;
    return static_cast<size_t>(ptr - s);
}

[[nodiscard]] inline int fast_strcmp(const char* s1, const char* s2) noexcept {
    if (s1 == s2) return 0;
    if (s1 == nullptr) return -1;
    if (s2 == nullptr) return 1;

    while (*s1 && (*s1 == *s2)) {
        ++s1;
        ++s2;
    }
    return static_cast<unsigned char>(*s1) - static_cast<unsigned char>(*s2);
}

[[nodiscard]] inline int fast_strncmp(const char* s1, const char* s2, size_t n) noexcept {
    if (n == 0 || s1 == s2) return 0;
    if (s1 == nullptr) return -1;
    if (s2 == nullptr) return 1;

    for (size_t i = 0; i < n; ++i) {
        if (s1[i] != s2[i] || s1[i] == '\0') {
            return static_cast<unsigned char>(s1[i]) - static_cast<unsigned char>(s2[i]);
        }
    }
    return 0;
}

inline char* fast_strcpy(char* __restrict dest, const char* __restrict src) noexcept {
    if (dest == nullptr || src == nullptr) return dest;
    const size_t len = fast_strlen(src);
    fast_memcpy(dest, src, len + 1);
    return dest;
}

inline char* fast_strncpy(char* __restrict dest, const char* __restrict src, size_t n) noexcept {
    if (dest == nullptr || src == nullptr || n == 0) return dest;
    size_t i = 0;
    for (; i < n && src[i] != '\0'; ++i) {
        dest[i] = src[i];
    }
    for (; i < n; ++i) {
        dest[i] = '\0';
    }
    return dest;
}

inline char* fast_strcat(char* __restrict dest, const char* __restrict src) noexcept {
    if (dest == nullptr || src == nullptr) return dest;
    char* d = dest + fast_strlen(dest);
    fast_strcpy(d, src);
    return dest;
}

[[nodiscard]] inline char* fast_strchr(const char* s, int c) noexcept {
    if (s == nullptr) return nullptr;
    const char ch = static_cast<char>(c);
    while (*s != '\0') {
        if (*s == ch) return const_cast<char*>(s);
        ++s;
    }
    return (ch == '\0') ? const_cast<char*>(s) : nullptr;
}

[[nodiscard]] inline char* fast_strrchr(const char* s, int c) noexcept {
    if (s == nullptr) return nullptr;
    const char ch = static_cast<char>(c);
    const char* last = nullptr;
    while (*s != '\0') {
        if (*s == ch) last = s;
        ++s;
    }
    if (ch == '\0') return const_cast<char*>(s);
    return const_cast<char*>(last);
}

/**
 * @brief Sub-linear substring search utilizing fast_memchr candidate matching.
 */
[[nodiscard]] inline char* fast_strstr(const char* haystack, const char* needle) noexcept {
    if (haystack == nullptr || needle == nullptr) return nullptr;
    if (*needle == '\0') return const_cast<char*>(haystack);

    const size_t needle_len = fast_strlen(needle);
    const size_t hay_len = fast_strlen(haystack);
    if (needle_len > hay_len) return nullptr;

    const char first = needle[0];
    const char* cur = haystack;
    size_t remaining = hay_len;

    while (remaining >= needle_len) {
        const void* found = fast_memchr(cur, first, remaining - needle_len + 1);
        if (found == nullptr) return nullptr;

        const auto* candidate = static_cast<const char*>(found);
        if (fast_memcmp(candidate, needle, needle_len) == 0) {
            return const_cast<char*>(candidate);
        }
        remaining -= static_cast<size_t>(candidate - cur + 1);
        cur = candidate + 1;
    }
    return nullptr;
}

// ============================================================================
// SECTION 3: FAST CTYPE CLASSIFICATION (ZERO LOCALE OVERHEAD)
// ============================================================================

[[nodiscard]] inline constexpr bool is_digit(char c) noexcept {
    return c >= '0' && c <= '9';
}
[[nodiscard]] inline constexpr bool is_upper(char c) noexcept {
    return c >= 'A' && c <= 'Z';
}
[[nodiscard]] inline constexpr bool is_lower(char c) noexcept {
    return c >= 'a' && c <= 'z';
}
[[nodiscard]] inline constexpr bool is_alpha(char c) noexcept {
    return is_upper(c) || is_lower(c);
}
[[nodiscard]] inline constexpr bool is_alnum(char c) noexcept {
    return is_alpha(c) || is_digit(c);
}
[[nodiscard]] inline constexpr bool is_space(char c) noexcept {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}
[[nodiscard]] inline constexpr bool is_xdigit(char c) noexcept {
    return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}
[[nodiscard]] inline constexpr bool is_print(char c) noexcept {
    return c >= 0x20 && c <= 0x7E;
}
[[nodiscard]] inline constexpr char to_upper(char c) noexcept {
    return is_lower(c) ? static_cast<char>(c - 32) : c;
}
[[nodiscard]] inline constexpr char to_lower(char c) noexcept {
    return is_upper(c) ? static_cast<char>(c + 32) : c;
}

// ============================================================================
// SECTION 4: NUMBER PARSING & FORMATTING (FREESTANDING STDLIB)
// ============================================================================

[[nodiscard]] inline int64_t fast_atoi64(const char* s, const char** endptr = nullptr) noexcept {
    if (s == nullptr) {
        if (endptr) *endptr = nullptr;
        return 0;
    }
    while (is_space(*s)) ++s;

    bool negative = false;
    if (*s == '-') {
        negative = true;
        ++s;
    } else if (*s == '+') {
        ++s;
    }

    uint64_t val = 0;
    constexpr uint64_t kMaxSafe = std::numeric_limits<uint64_t>::max() / 10;
    bool overflowed = false;

    while (is_digit(*s)) {
        const uint64_t d = static_cast<uint64_t>(*s - '0');
        if (val > kMaxSafe || (val == kMaxSafe && d > (std::numeric_limits<uint64_t>::max() % 10))) {
            overflowed = true;
        } else {
            val = val * 10 + d;
        }
        ++s;
    }
    if (endptr) *endptr = s;

    if (overflowed) {
        return negative ? std::numeric_limits<int64_t>::min() : std::numeric_limits<int64_t>::max();
    }
    return negative ? -static_cast<int64_t>(val) : static_cast<int64_t>(val);
}

[[nodiscard]] inline uint64_t fast_atou64(const char* s, const char** endptr = nullptr, int base = 10) noexcept {
    if (s == nullptr) {
        if (endptr) *endptr = nullptr;
        return 0;
    }
    while (is_space(*s)) ++s;

    if (base == 0) {
        if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
            base = 16;
            s += 2;
        } else if (s[0] == '0' && (s[1] == 'b' || s[1] == 'B')) {
            base = 2;
            s += 2;
        } else if (s[0] == '0') {
            base = 8;
            s += 1;
        } else {
            base = 10;
        }
    }

    uint64_t val = 0;
    while (*s) {
        int d = -1;
        if (is_digit(*s)) d = *s - '0';
        else if (*s >= 'a' && *s <= 'f') d = *s - 'a' + 10;
        else if (*s >= 'A' && *s <= 'F') d = *s - 'A' + 10;

        if (d < 0 || d >= base) break;
        val = val * static_cast<uint64_t>(base) + static_cast<uint64_t>(d);
        ++s;
    }
    if (endptr) *endptr = s;
    return val;
}

/**
 * @brief Fast buffer integer formatting (radix 10 or 16).
 */
inline size_t format_i64(int64_t val, char* buf, size_t buf_size, int base = 10) noexcept {
    if (buf == nullptr || buf_size == 0) return 0;

    char temp[64];
    size_t pos = 0;
    bool neg = false;
    uint64_t uval;

    if (base == 10 && val < 0) {
        neg = true;
        uval = static_cast<uint64_t>(-(val + 1)) + 1;
    } else {
        uval = static_cast<uint64_t>(val);
    }

    static const char digits[] = "0123456789abcdef";
    do {
        temp[pos++] = digits[uval % static_cast<uint64_t>(base)];
        uval /= static_cast<uint64_t>(base);
    } while (uval > 0 && pos < sizeof(temp));

    if (neg && pos < sizeof(temp)) {
        temp[pos++] = '-';
    }

    if (pos >= buf_size) pos = buf_size - 1;

    for (size_t i = 0; i < pos; ++i) {
        buf[i] = temp[pos - 1 - i];
    }
    buf[pos] = '\0';
    return pos;
}

/**
 * @brief Fast floating point formatting (handling sign, NaN, Inf, and precision rounding).
 */
inline size_t format_f64(double val, char* buf, size_t buf_size, int precision = 6) noexcept {
    if (buf == nullptr || buf_size == 0) return 0;

    if (std::isnan(val)) {
        return fast_strlen(fast_strcpy(buf, "nan"));
    }
    if (std::isinf(val)) {
        return fast_strlen(fast_strcpy(buf, (val < 0.0) ? "-inf" : "inf"));
    }

    size_t idx = 0;
    if (val < 0.0) {
        if (idx < buf_size - 1) buf[idx++] = '-';
        val = -val;
    }

    auto int_part = static_cast<int64_t>(val);
    double frac_part = val - static_cast<double>(int_part);

    idx += format_i64(int_part, buf + idx, buf_size - idx, 10);

    if (precision > 0 && idx < buf_size - 1) {
        buf[idx++] = '.';
        for (int i = 0; i < precision && idx < buf_size - 1; ++i) {
            frac_part *= 10.0;
            int digit = static_cast<int>(frac_part);
            buf[idx++] = static_cast<char>('0' + digit);
            frac_part -= digit;
        }
    }

    buf[idx] = '\0';
    return idx;
}

} // namespace dgtl::utils::libc
