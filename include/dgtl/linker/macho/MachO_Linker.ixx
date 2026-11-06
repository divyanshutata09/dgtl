/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Component:   Sovereign macOS 64-bit Mach-O Standalone Executable Linker
 * Standards:   C++26 / Clang 18+ / MSVC 19.40+ / GCC 14+
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

module;

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>

export module dgtl.linker.macho;

import dgtl.object.file;
import dgtl.object.section;
import dgtl.object.symbol;
import dgtl.object.relocation;
import dgtl.linker.symbols;
export import dgtl.linker.macho.sections;
export import dgtl.linker.macho.relocations;

export namespace dgtl::linker::macho {

/* ============================================================================
 * Sovereign In-Memory SHA-256 Cryptographic Hasher for Apple Silicon
 * ========================================================================== */

namespace sha256 {

inline constexpr uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

[[nodiscard]] inline constexpr uint32_t rotr(uint32_t x, uint32_t n) noexcept {
    return (x >> n) | (x << (32 - n));
}

inline void process_block(const uint8_t* chunk, uint32_t* state) noexcept {
    uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
        w[i] = (static_cast<uint32_t>(chunk[i * 4]) << 24) |
               (static_cast<uint32_t>(chunk[i * 4 + 1]) << 16) |
               (static_cast<uint32_t>(chunk[i * 4 + 2]) << 8) |
               (static_cast<uint32_t>(chunk[i * 4 + 3]));
    }
    for (int i = 16; i < 64; ++i) {
        const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    uint32_t e = state[4], f = state[5], g = state[6], h = state[7];

    for (int i = 0; i < 64; ++i) {
        const uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        const uint32_t ch = (e & f) ^ ((~e) & g);
        const uint32_t temp1 = h + S1 + ch + K[i] + w[i];
        const uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        const uint32_t temp2 = S0 + maj;

        h = g; g = f; f = e; e = d + temp1;
        d = c; c = b; b = a; a = temp1 + temp2;
    }

    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

inline void hash(const uint8_t* data, size_t length, uint8_t* out32) noexcept {
    uint32_t state[8] = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
    };

    size_t i = 0;
    for (; i + 64 <= length; i += 64) {
        process_block(data + i, state);
    }

    uint8_t buffer[128] = {0};
    const size_t rem = length - i;
    if (rem > 0) {
        std::memcpy(buffer, data + i, rem);
    }
    buffer[rem] = 0x80;

    const size_t total_blocks = (rem + 1 + 8 > 64) ? 2 : 1;
    const uint64_t bit_len = static_cast<uint64_t>(length) * 8;
    for (int b = 0; b < 8; ++b) {
        buffer[total_blocks * 64 - 1 - b] = static_cast<uint8_t>((bit_len >> (b * 8)) & 0xFF);
    }

    for (size_t b = 0; b < total_blocks; ++b) {
        process_block(buffer + b * 64, state);
    }

    for (int idx = 0; idx < 8; ++idx) {
        out32[idx * 4 + 0] = static_cast<uint8_t>((state[idx] >> 24) & 0xFF);
        out32[idx * 4 + 1] = static_cast<uint8_t>((state[idx] >> 16) & 0xFF);
        out32[idx * 4 + 2] = static_cast<uint8_t>((state[idx] >> 8) & 0xFF);
        out32[idx * 4 + 3] = static_cast<uint8_t>(state[idx] & 0xFF);
    }
}

} // namespace sha256

/* ============================================================================
 * Sovereign Mach-O Executable Linker
 * ========================================================================== */

struct SovereignMachOLinkerOptions {
    uint64_t image_base = 0x100000000ULL; // Standard macOS 64-bit base (4GB)
    int32_t  cpu_type = CPU_TYPE_ARM64_VAL; // Default to ARM64 (Apple Silicon)
    int32_t  cpu_subtype = CPU_SUBTYPE_ALL_VAL;
    std::string entry_symbol = "main";
    bool emit_ad_hoc_signature = true;   // Sovereign Apple Silicon code signing
    std::string dylinker = "/usr/lib/dyld";
    std::string default_dylib = "/usr/lib/libSystem.B.dylib";
    uint32_t page_size = 0x4000;         // 16 KB page size standard on Apple Silicon
    bool sovereign = true;
};

class SovereignMachOLinker {
public:
    using Options = SovereignMachOLinkerOptions;

    static std::vector<uint8_t> get_sovereign_intrinsic_arm64(std::string_view name) {
        if (name.starts_with("__imp_")) name.remove_prefix(6);
        if (name == "__dgtl_rt_sqrt" || name == "dgtl_rt_sqrt") {
            return { 0x00, 0x38, 0x61, 0x1E, 0xC0, 0x03, 0x5F, 0xD6 }; // fsqrt d0, d0; ret
        }
        if (name == "__dgtl_rt_sqrtf" || name == "dgtl_rt_sqrtf") {
            return { 0x00, 0x38, 0x21, 0x1E, 0xC0, 0x03, 0x5F, 0xD6 }; // fsqrt s0, s0; ret
        }
        if (name == "__dgtl_rt_fast_inv_sqrt" || name == "dgtl_rt_fast_inv_sqrt" ||
            name == "__dgtl_rt_fast_inv_sqrt_bits" || name == "dgtl_rt_fast_inv_sqrt_bits") {
            return { 0x01, 0xD8, 0x2C, 0x7E, 0x20, 0x40, 0x20, 0x1E, 0xC0, 0x03, 0x5F, 0xD6 }; // frsqrte; fmov; ret
        }
        if (name == "__dgtl_rt_abs" || name == "dgtl_rt_abs" || name == "__dgtl_rt_fabs" || name == "dgtl_rt_fabs") {
            return { 0x00, 0x20, 0x60, 0x1E, 0xC0, 0x03, 0x5F, 0xD6 }; // fabs d0, d0; ret
        }
        if (name == "__dgtl_rt_absf" || name == "dgtl_rt_absf" || name == "__dgtl_rt_fabsf" || name == "dgtl_rt_fabsf") {
            return { 0x00, 0x20, 0x20, 0x1E, 0xC0, 0x03, 0x5F, 0xD6 }; // fabs s0, s0; ret
        }
        if (name == "__dgtl_rt_floor" || name == "dgtl_rt_floor") return { 0x00, 0x50, 0x61, 0x1E, 0xC0, 0x03, 0x5F, 0xD6 };
        if (name == "__dgtl_rt_floorf" || name == "dgtl_rt_floorf") return { 0x00, 0x50, 0x21, 0x1E, 0xC0, 0x03, 0x5F, 0xD6 };
        if (name == "__dgtl_rt_ceil"  || name == "dgtl_rt_ceil")  return { 0x00, 0x48, 0x61, 0x1E, 0xC0, 0x03, 0x5F, 0xD6 };
        if (name == "__dgtl_rt_ceilf"  || name == "dgtl_rt_ceilf")  return { 0x00, 0x48, 0x21, 0x1E, 0xC0, 0x03, 0x5F, 0xD6 };
        if (name == "__dgtl_rt_round" || name == "dgtl_rt_round") return { 0x00, 0x60, 0x61, 0x1E, 0xC0, 0x03, 0x5F, 0xD6 };
        if (name == "__dgtl_rt_roundf" || name == "dgtl_rt_roundf") return { 0x00, 0x60, 0x21, 0x1E, 0xC0, 0x03, 0x5F, 0xD6 };
        if (name == "__dgtl_rt_trunc" || name == "dgtl_rt_trunc") return { 0x00, 0x58, 0x61, 0x1E, 0xC0, 0x03, 0x5F, 0xD6 };
        if (name == "__dgtl_rt_fma_f32" || name == "dgtl_rt_fma_f32") return { 0x00, 0x08, 0x21, 0x1F, 0xC0, 0x03, 0x5F, 0xD6 };
        if (name == "__dgtl_rt_fma_f64" || name == "dgtl_rt_fma_f64") return { 0x00, 0x08, 0x61, 0x1F, 0xC0, 0x03, 0x5F, 0xD6 };
        if (name == "__dgtl_rt_relu" || name == "dgtl_rt_relu" || name == "relu") {
            return { 0xE1, 0x03, 0x27, 0x9E, 0x00, 0x48, 0x61, 0x1E, 0xC0, 0x03, 0x5F, 0xD6 }; // fmov d1, xzr; fmax d0, d0, d1; ret
        }
        if (name == "__dgtl_rt_reluf" || name == "dgtl_rt_reluf" || name == "reluf") {
            return { 0xE1, 0x03, 0x27, 0x1E, 0x00, 0x48, 0x21, 0x1E, 0xC0, 0x03, 0x5F, 0xD6 }; // fmov s1, wzr; fmax s0, s0, s1; ret
        }
        if (name == "memcpy" || name == "__memcpy") {
            return { 0xE3, 0x03, 0x00, 0xAA, 0x82, 0x00, 0x00, 0xB4, 0x24, 0x14, 0x40, 0x38, 0x64, 0x14, 0x00, 0x38, 0x42, 0x04, 0x00, 0xF1, 0xAC, 0xFF, 0xFF, 0x54, 0xC0, 0x03, 0x5F, 0xD6 };
        }
        if (name == "memset" || name == "__memset") {
            return { 0xE3, 0x03, 0x00, 0xAA, 0x82, 0x00, 0x00, 0xB4, 0x61, 0x14, 0x00, 0x38, 0x42, 0x04, 0x00, 0xF1, 0xCD, 0xFF, 0xFF, 0x54, 0xC0, 0x03, 0x5F, 0xD6 };
        }
        if (name == "memcmp" || name == "__memcmp") {
            return { 0xC2, 0x00, 0x00, 0xB4, 0x03, 0x14, 0x40, 0x38, 0x24, 0x14, 0x40, 0x38, 0x7F, 0x00, 0x04, 0x6B, 0x81, 0x00, 0x00, 0x54, 0x42, 0x04, 0x00, 0xF1, 0x61, 0xFF, 0xFF, 0x54, 0x00, 0x00, 0x80, 0x52, 0xC0, 0x03, 0x5F, 0xD6, 0x60, 0x00, 0x04, 0x4B, 0xC0, 0x03, 0x5F, 0xD6 };
        }
        if (name == "memmove" || name == "__memmove") {
            return { 0xE3, 0x03, 0x00, 0xAA, 0x82, 0x00, 0x00, 0xB4, 0x24, 0x14, 0x40, 0x38, 0x64, 0x14, 0x00, 0x38, 0x42, 0x04, 0x00, 0xF1, 0xAC, 0xFF, 0xFF, 0x54, 0xC0, 0x03, 0x5F, 0xD6 };
        }
        if (name == "__dgtl_rt_bits_to_f64" || name == "dgtl_rt_bits_to_f64" || name == "bits_to_f64") {
            return { 0x00, 0x00, 0x67, 0x9E, 0xC0, 0x03, 0x5F, 0xD6 }; // fmov d0, x0; ret
        }
        if (name == "__dgtl_rt_f64_to_bits" || name == "dgtl_rt_f64_to_bits" || name == "f64_to_bits") {
            return { 0x00, 0x00, 0x7E, 0x9E, 0xC0, 0x03, 0x5F, 0xD6 }; // fmov x0, d0; ret
        }
        if (name == "__dgtl_rt_fmod" || name == "dgtl_rt_fmod" || name == "fmod" || name == "fmodf") {
            return { 0x02, 0x18, 0x61, 0x1E, 0x42, 0x58, 0x61, 0x1E, 0x02, 0x08, 0x61, 0x1E, 0x00, 0x38, 0x62, 0x1E, 0xC0, 0x03, 0x5F, 0xD6 };
        }
        if (name == "__dgtl_rt_i64_pow" || name == "__myzon_i64_pow" || name == "i64_pow" || name == "pow_bits") {
            return {
                0xE2, 0x03, 0x00, 0xAA, // mov x2, x0
                0x20, 0x00, 0x80, 0xD2, // mov x0, #1
                0x81, 0x00, 0x00, 0xB4, // cbz x1, .done (+16)
                0x41, 0x00, 0x00, 0x36, // tbz x1, #0, .skip (+8)
                0x00, 0x7C, 0x02, 0x9B, // mul x0, x0, x2
                0x42, 0x7C, 0x02, 0x9B, // mul x2, x2, x2
                0x21, 0x04, 0x41, 0xD3, // lsr x1, x1, #1
                0xA1, 0xFF, 0xFF, 0x35, // cbnz x1, .loop (-12)
                0xC0, 0x03, 0x5F, 0xD6  // ret
            };
        }
        return {};
    }

    static std::vector<uint8_t> get_sovereign_intrinsic_x64(std::string_view name) {
        if (name.starts_with("__imp_")) name.remove_prefix(6);
        if (name == "__dgtl_rt_sqrt" || name == "dgtl_rt_sqrt") return { 0xF2, 0x0F, 0x51, 0xC0, 0xC3 };
        if (name == "__dgtl_rt_sqrtf" || name == "dgtl_rt_sqrtf") return { 0xF3, 0x0F, 0x51, 0xC0, 0xC3 };
        if (name == "__dgtl_rt_fast_inv_sqrt" || name == "dgtl_rt_fast_inv_sqrt" ||
            name == "__dgtl_rt_fast_inv_sqrt_bits" || name == "dgtl_rt_fast_inv_sqrt_bits") {
            return { 0xF3, 0x0F, 0x52, 0xC8, 0x0F, 0x28, 0xC1, 0xC3 };
        }
        if (name == "__dgtl_rt_abs" || name == "dgtl_rt_abs" || name == "__dgtl_rt_fabs" || name == "dgtl_rt_fabs") {
            return { 0x66, 0x48, 0x0F, 0x7E, 0xC0, 0x48, 0x0F, 0xBA, 0xF0, 0x3F, 0x66, 0x48, 0x0F, 0x6E, 0xC0, 0xC3 };
        }
        if (name == "__dgtl_rt_absf" || name == "dgtl_rt_absf" || name == "__dgtl_rt_fabsf" || name == "dgtl_rt_fabsf") {
            return { 0x66, 0x0F, 0x7E, 0xC0, 0x0F, 0xBA, 0xF0, 0x1F, 0x66, 0x0F, 0x6E, 0xC0, 0xC3 };
        }
        if (name == "__dgtl_rt_floor" || name == "dgtl_rt_floor") return { 0x66, 0x0F, 0x3A, 0x0B, 0xC0, 0x01, 0xC3 };
        if (name == "__dgtl_rt_floorf" || name == "dgtl_rt_floorf") return { 0x66, 0x0F, 0x3A, 0x0A, 0xC0, 0x01, 0xC3 };
        if (name == "__dgtl_rt_ceil" || name == "dgtl_rt_ceil")   return { 0x66, 0x0F, 0x3A, 0x0B, 0xC0, 0x02, 0xC3 };
        if (name == "__dgtl_rt_ceilf" || name == "dgtl_rt_ceilf")   return { 0x66, 0x0F, 0x3A, 0x0A, 0xC0, 0x02, 0xC3 };
        if (name == "__dgtl_rt_round" || name == "dgtl_rt_round") return { 0x66, 0x0F, 0x3A, 0x0B, 0xC0, 0x00, 0xC3 };
        if (name == "__dgtl_rt_roundf" || name == "dgtl_rt_roundf") return { 0x66, 0x0F, 0x3A, 0x0A, 0xC0, 0x00, 0xC3 };
        if (name == "__dgtl_rt_trunc" || name == "dgtl_rt_trunc") return { 0x66, 0x0F, 0x3A, 0x0B, 0xC0, 0x03, 0xC3 };
        if (name == "__dgtl_rt_truncf" || name == "dgtl_rt_truncf") return { 0x66, 0x0F, 0x3A, 0x0A, 0xC0, 0x03, 0xC3 };
        if (name == "__dgtl_rt_read_tsc" || name == "dgtl_rt_read_tsc") {
            return { 0x0F, 0x31, 0x48, 0xC1, 0xE2, 0x20, 0x48, 0x09, 0xD0, 0xC3 };
        }
        if (name == "__dgtl_rt_prefetch" || name == "dgtl_rt_prefetch" || name == "__dgtl_rt_prefetch_t0" || name == "dgtl_rt_prefetch_t0") {
            return { 0x0F, 0x18, 0x01, 0xC3 };
        }
        if (name == "__dgtl_rt_clflush" || name == "dgtl_rt_clflush") return { 0x0F, 0xAE, 0x39, 0xC3 };
        if (name == "__dgtl_rt_fma_f32" || name == "dgtl_rt_fma_f32") return { 0xC4, 0xE2, 0x71, 0xA9, 0xC2, 0xC3 };
        if (name == "__dgtl_rt_fma_f64" || name == "dgtl_rt_fma_f64") return { 0xC4, 0xE2, 0xF9, 0xA9, 0xC2, 0xC3 };
        if (name == "__dgtl_rt_relu" || name == "dgtl_rt_relu" || name == "relu") {
            return { 0x66, 0x0F, 0x57, 0xC9, 0xF2, 0x0F, 0x5F, 0xC1, 0xC3 }; // xorpd xmm1, xmm1; maxsd xmm0, xmm1; ret
        }
        if (name == "__dgtl_rt_reluf" || name == "dgtl_rt_reluf" || name == "reluf") {
            return { 0x0F, 0x57, 0xC9, 0x0F, 0x5F, 0xC1, 0xC3 }; // xorps xmm1, xmm1; maxss xmm0, xmm1; ret
        }
        if (name == "__dgtl_rt_sin" || name == "dgtl_rt_sin") {
            return { 0x48, 0x83, 0xEC, 0x08, 0xF2, 0x0F, 0x11, 0x04, 0x24, 0xDD, 0x04, 0x24, 0xD9, 0xFE, 0xDD, 0x1C, 0x24, 0xF2, 0x0F, 0x10, 0x04, 0x24, 0x48, 0x83, 0xC4, 0x08, 0xC3 };
        }
        if (name == "__dgtl_rt_cos" || name == "dgtl_rt_cos") {
            return { 0x48, 0x83, 0xEC, 0x08, 0xF2, 0x0F, 0x11, 0x04, 0x24, 0xDD, 0x04, 0x24, 0xD9, 0xFF, 0xDD, 0x1C, 0x24, 0xF2, 0x0F, 0x10, 0x04, 0x24, 0x48, 0x83, 0xC4, 0x08, 0xC3 };
        }
        if (name == "__dgtl_rt_tan" || name == "dgtl_rt_tan") {
            return { 0x48, 0x83, 0xEC, 0x08, 0xF2, 0x0F, 0x11, 0x04, 0x24, 0xDD, 0x04, 0x24, 0xD9, 0xF2, 0xDD, 0xD8, 0xDD, 0x1C, 0x24, 0xF2, 0x0F, 0x10, 0x04, 0x24, 0x48, 0x83, 0xC4, 0x08, 0xC3 };
        }
        if (name == "__dgtl_rt_atan" || name == "dgtl_rt_atan") {
            return { 0x48, 0x83, 0xEC, 0x08, 0xF2, 0x0F, 0x11, 0x04, 0x24, 0xDD, 0x04, 0x24, 0xD9, 0xE8, 0xD9, 0xF3, 0xDD, 0x1C, 0x24, 0xF2, 0x0F, 0x10, 0x04, 0x24, 0x48, 0x83, 0xC4, 0x08, 0xC3 };
        }
        if (name == "__dgtl_rt_atan2" || name == "dgtl_rt_atan2") {
            return { 0x48, 0x83, 0xEC, 0x10, 0xF2, 0x0F, 0x11, 0x04, 0x24, 0xF2, 0x0F, 0x11, 0x4C, 0x24, 0x08, 0xDD, 0x04, 0x24, 0xDD, 0x44, 0x24, 0x08, 0xD9, 0xF3, 0xDD, 0x1C, 0x24, 0xF2, 0x0F, 0x10, 0x04, 0x24, 0x48, 0x83, 0xC4, 0x10, 0xC3 };
        }
        if (name == "__dgtl_rt_log" || name == "dgtl_rt_log") {
            return { 0x48, 0x83, 0xEC, 0x08, 0xF2, 0x0F, 0x11, 0x04, 0x24, 0xD9, 0xED, 0xDD, 0x04, 0x24, 0xD9, 0xF1, 0xDD, 0x1C, 0x24, 0xF2, 0x0F, 0x10, 0x04, 0x24, 0x48, 0x83, 0xC4, 0x08, 0xC3 };
        }
        if (name == "__dgtl_rt_log2" || name == "dgtl_rt_log2") {
            return { 0x48, 0x83, 0xEC, 0x08, 0xF2, 0x0F, 0x11, 0x04, 0x24, 0xD9, 0xE8, 0xDD, 0x04, 0x24, 0xD9, 0xF1, 0xDD, 0x1C, 0x24, 0xF2, 0x0F, 0x10, 0x04, 0x24, 0x48, 0x83, 0xC4, 0x08, 0xC3 };
        }
        if (name == "__dgtl_rt_log10" || name == "dgtl_rt_log10") {
            return { 0x48, 0x83, 0xEC, 0x08, 0xF2, 0x0F, 0x11, 0x04, 0x24, 0xD9, 0xEC, 0xDD, 0x04, 0x24, 0xD9, 0xF1, 0xDD, 0x1C, 0x24, 0xF2, 0x0F, 0x10, 0x04, 0x24, 0x48, 0x83, 0xC4, 0x08, 0xC3 };
        }
        if (name == "__dgtl_rt_exp" || name == "dgtl_rt_exp") {
            return { 0x48, 0x83, 0xEC, 0x08, 0xF2, 0x0F, 0x11, 0x04, 0x24, 0xD9, 0xEA, 0xDC, 0x0C, 0x24, 0xD9, 0xC0, 0xD9, 0xFC, 0xDC, 0xE9, 0xD9, 0xC9, 0xD9, 0xF0, 0xD9, 0xE8, 0xDE, 0xC1, 0xD9, 0xFD, 0xDD, 0xD9, 0xDD, 0x1C, 0x24, 0xF2, 0x0F, 0x10, 0x04, 0x24, 0x48, 0x83, 0xC4, 0x08, 0xC3 };
        }
        if (name == "__dgtl_rt_pow" || name == "dgtl_rt_pow") {
            return { 0x48, 0x83, 0xEC, 0x10, 0xF2, 0x0F, 0x11, 0x04, 0x24, 0xF2, 0x0F, 0x11, 0x4C, 0x24, 0x08, 0xDD, 0x44, 0x24, 0x08, 0xDD, 0x04, 0x24, 0xD9, 0xF1, 0xD9, 0xC0, 0xD9, 0xFC, 0xDC, 0xE9, 0xD9, 0xC9, 0xD9, 0xF0, 0xD9, 0xE8, 0xDE, 0xC1, 0xD9, 0xFD, 0xDD, 0xD9, 0xDD, 0x1C, 0x24, 0xF2, 0x0F, 0x10, 0x04, 0x24, 0x48, 0x83, 0xC4, 0x10, 0xC3 };
        }
        if (name == "__dgtl_rt_sinf" || name == "dgtl_rt_sinf") {
            return { 0x48, 0x83, 0xEC, 0x08, 0xF3, 0x0F, 0x11, 0x04, 0x24, 0xD9, 0x04, 0x24, 0xD9, 0xFE, 0xD9, 0x1C, 0x24, 0xF3, 0x0F, 0x10, 0x04, 0x24, 0x48, 0x83, 0xC4, 0x08, 0xC3 };
        }
        if (name == "__dgtl_rt_cosf" || name == "dgtl_rt_cosf") {
            return { 0x48, 0x83, 0xEC, 0x08, 0xF3, 0x0F, 0x11, 0x04, 0x24, 0xD9, 0x04, 0x24, 0xD9, 0xFF, 0xD9, 0x1C, 0x24, 0xF3, 0x0F, 0x10, 0x04, 0x24, 0x48, 0x83, 0xC4, 0x08, 0xC3 };
        }
        if (name == "__dgtl_rt_tanf" || name == "dgtl_rt_tanf") {
            return { 0x48, 0x83, 0xEC, 0x08, 0xF3, 0x0F, 0x11, 0x04, 0x24, 0xD9, 0x04, 0x24, 0xD9, 0xF2, 0xDD, 0xD8, 0xD9, 0x1C, 0x24, 0xF3, 0x0F, 0x10, 0x04, 0x24, 0x48, 0x83, 0xC4, 0x08, 0xC3 };
        }
        if (name == "__dgtl_rt_logf" || name == "dgtl_rt_logf") {
            return { 0x48, 0x83, 0xEC, 0x08, 0xF3, 0x0F, 0x11, 0x04, 0x24, 0xD9, 0xED, 0xD9, 0x04, 0x24, 0xD9, 0xF1, 0xD9, 0x1C, 0x24, 0xF3, 0x0F, 0x10, 0x04, 0x24, 0x48, 0x83, 0xC4, 0x08, 0xC3 };
        }
        if (name == "__dgtl_rt_expf" || name == "dgtl_rt_expf") {
            return { 0x48, 0x83, 0xEC, 0x08, 0xF3, 0x0F, 0x11, 0x04, 0x24, 0xD9, 0xEA, 0xD8, 0x0C, 0x24, 0xD9, 0xC0, 0xD9, 0xFC, 0xDC, 0xE9, 0xD9, 0xC9, 0xD9, 0xF0, 0xD9, 0xE8, 0xDE, 0xC1, 0xD9, 0xFD, 0xDD, 0xD9, 0xD9, 0x1C, 0x24, 0xF3, 0x0F, 0x10, 0x04, 0x24, 0x48, 0x83, 0xC4, 0x08, 0xC3 };
        }
        if (name == "__dgtl_rt_powf" || name == "dgtl_rt_powf") {
            return { 0x48, 0x83, 0xEC, 0x10, 0xF3, 0x0F, 0x11, 0x04, 0x24, 0xF3, 0x0F, 0x11, 0x4C, 0x24, 0x04, 0xD9, 0x44, 0x24, 0x04, 0xD9, 0x04, 0x24, 0xD9, 0xF1, 0xD9, 0xC0, 0xD9, 0xFC, 0xDC, 0xE9, 0xD9, 0xC9, 0xD9, 0xF0, 0xD9, 0xE8, 0xDE, 0xC1, 0xD9, 0xFD, 0xDD, 0xD9, 0xD9, 0x1C, 0x24, 0xF3, 0x0F, 0x10, 0x04, 0x24, 0x48, 0x83, 0xC4, 0x10, 0xC3 };
        }
        if (name == "__dgtl_rt_fmod" || name == "dgtl_rt_fmod") {
            return { 0x48, 0x83, 0xEC, 0x10, 0xF2, 0x0F, 0x11, 0x04, 0x24, 0xF2, 0x0F, 0x11, 0x4C, 0x24, 0x08, 0xDD, 0x44, 0x24, 0x08, 0xDD, 0x04, 0x24, 0xD9, 0xF8, 0xDD, 0x1C, 0x24, 0xDD, 0xD8, 0xF2, 0x0F, 0x10, 0x04, 0x24, 0x48, 0x83, 0xC4, 0x10, 0xC3 };
        }
        if (name == "__dgtl_rt_vec4_dot" || name == "dgtl_rt_vec4_dot") {
            return { 0x0F, 0x10, 0x01, 0x0F, 0x10, 0x0A, 0x66, 0x0F, 0x3A, 0x40, 0xC1, 0xF1, 0xC3 };
        }
        if (name == "__dgtl_rt_vec3_cross" || name == "dgtl_rt_vec3_cross") {
            return {
                0xF3, 0x0F, 0x10, 0x42, 0x04, 0xF3, 0x0F, 0x59, 0x40, 0x08, 0xF3, 0x0F, 0x10, 0x4A, 0x08, 0xF3, 0x0F, 0x59, 0x48, 0x04,
                0xF3, 0x0F, 0x5C, 0xC1, 0xF3, 0x0F, 0x11, 0x01, 0xF3, 0x0F, 0x10, 0x42, 0x08, 0xF3, 0x0F, 0x59, 0x00, 0xF3, 0x0F, 0x10, 0x0A,
                0xF3, 0x0F, 0x59, 0x48, 0x08, 0xF3, 0x0F, 0x5C, 0xC1, 0xF3, 0x0F, 0x11, 0x41, 0x04, 0xF3, 0x0F, 0x10, 0x02, 0xF3, 0x0F, 0x59, 0x48, 0x04,
                0xF3, 0x0F, 0x10, 0x4A, 0x04, 0xF3, 0x0F, 0x59, 0x00, 0xF3, 0x0F, 0x5C, 0xC1, 0xF3, 0x0F, 0x11, 0x41, 0x08, 0xC3
            };
        }
        if (name == "memcpy" || name == "__memcpy") {
            // System V AMD64 ABI: rdi=dest, rsi=src, rdx=count. Return dest in rax.
            return {
                0x48, 0x89, 0xF8,             // mov rax, rdi
                0x48, 0x89, 0xD1,             // mov rcx, rdx
                0xF3, 0xA4,                   // rep movsb
                0xC3                          // ret
            };
        }
        if (name == "memset" || name == "__memset") {
            // System V AMD64 ABI: rdi=dest, rsi=val, rdx=count. Return dest in rax.
            return {
                0x49, 0x89, 0xF8,             // mov r8, rdi
                0x48, 0x89, 0xF0,             // mov rax, rsi
                0x48, 0x89, 0xD1,             // mov rcx, rdx
                0xF3, 0xAA,                   // rep stosb
                0x4C, 0x89, 0xC0,             // mov rax, r8
                0xC3                          // ret
            };
        }
        if (name == "memcmp" || name == "__memcmp") {
            // System V AMD64 ABI: rdi=s1, rsi=s2, rdx=count.
            return {
                0x48, 0x85, 0xD2,             // test rdx, rdx
                0x74, 0x12,                   // jz .equal
                0x48, 0x89, 0xD1,             // mov rcx, rdx
                0xF3, 0xA6,                   // repe cmpsb
                0x74, 0x0B,                   // je .equal
                0x0F, 0xB6, 0x47, 0xFF,       // movzx eax, byte ptr [rdi-1]
                0x0F, 0xB6, 0x56, 0xFF,       // movzx edx, byte ptr [rsi-1]
                0x29, 0xD0,                   // sub eax, edx
                0xC3,                         // ret
                0x31, 0xC0,                   // .equal: xor eax, eax
                0xC3                          // ret
            };
        }
        if (name == "memmove" || name == "__memmove") {
            // System V AMD64 ABI: rdi=dest, rsi=src, rdx=count. Return dest in rax.
            return {
                0x48, 0x89, 0xF8,                         // mov rax, rdi
                0x48, 0x85, 0xD2,                         // test rdx, rdx
                0x74, 0x1D,                               // jz .done
                0x48, 0x39, 0xF7,                         // cmp rdi, rsi
                0x76, 0x12,                               // jbe .forward
                0x48, 0x8D, 0x7C, 0x17, 0xFF,             // lea rdi, [rdi+rdx-1]
                0x48, 0x8D, 0x74, 0x16, 0xFF,             // lea rsi, [rsi+rdx-1]
                0x48, 0x89, 0xD1,                         // mov rcx, rdx
                0xFD,                                     // std
                0xF3, 0xA4,                               // rep movsb
                0xFC,                                     // cld
                0xC3,                                     // ret
                0x48, 0x89, 0xD1,                         // .forward: mov rcx, rdx
                0xFC,                                     // cld
                0xF3, 0xA4,                               // rep movsb
                0xC3                                      // .done: ret
            };
        }
        if (name == "__chkstk" || name == "_chkstk" || name == "___chkstk_ms" || name == "__chkstk_ms" || name == "_alloca" || name == "__alloca") {
            // Sovereign x64 Stack Probe (__chkstk / ___chkstk_ms)
            return {
                0x51,                                     // push rcx
                0x52,                                     // push rdx
                0x50,                                     // push rax
                0x9C,                                     // pushfq
                0x48, 0x8D, 0x4C, 0x24, 0x28,             // lea rcx, [rsp + 40] (caller RSP)
                0x48, 0x89, 0xC2,                         // mov rdx, rax (bytes to probe)
                // .probe_pages:
                0x48, 0x81, 0xFA, 0x00, 0x10, 0x00, 0x00, // cmp rdx, 4096
                0x72, 0x12,                               // jb .probe_tail (+18)
                0x48, 0x81, 0xE9, 0x00, 0x10, 0x00, 0x00, // sub rcx, 4096
                0x85, 0x01,                               // test [rcx], eax (probe guard page)
                0x48, 0x81, 0xEA, 0x00, 0x10, 0x00, 0x00, // sub rdx, 4096
                0xEB, 0xE5,                               // jmp .probe_pages (-27)
                // .probe_tail:
                0x48, 0x29, 0xD1,                         // sub rcx, rdx
                0x85, 0x01,                               // test [rcx], eax (probe remainder)
                0x9D,                                     // popfq
                0x58,                                     // pop rax
                0x5A,                                     // pop rdx
                0x59,                                     // pop rcx
                0xC3                                      // ret
            };
        }
        if (name == "__dgtl_rt_bits_to_f64" || name == "dgtl_rt_bits_to_f64" || name == "bits_to_f64") {
            return { 0x66, 0x48, 0x0F, 0x6E, 0xC7, 0xC3 }; // movq xmm0, rdi; ret
        }
        if (name == "__dgtl_rt_f64_to_bits" || name == "dgtl_rt_f64_to_bits" || name == "f64_to_bits") {
            return { 0x66, 0x48, 0x0F, 0x7E, 0xC0, 0xC3 }; // movq rax, xmm0; ret
        }
        if (name == "__dgtl_rt_i64_pow" || name == "__myzon_i64_pow" || name == "i64_pow" || name == "pow_bits") {
            return {
                0x48, 0xC7, 0xC0, 0x01, 0x00, 0x00, 0x00, // mov rax, 1
                0x48, 0x85, 0xF6,                         // test rsi, rsi
                0x74, 0x14,                               // jz .done (+20)
                // .loop:
                0x40, 0xF6, 0xC6, 0x01,                   // test sil, 1
                0x74, 0x04,                               // jz .skip (+4)
                0x48, 0x0F, 0xAF, 0xC7,                   // imul rax, rdi
                // .skip:
                0x48, 0x0F, 0xAF, 0xFF,                   // imul rdi, rdi
                0x48, 0xD1, 0xEE,                         // shr rsi, 1
                0x75, 0xEE,                               // jnz .loop (-18)
                0xC3                                      // ret
            };
        }
        return {};
    }

    static bool link_executable(
        const dgtl::object::ObjectFile& obj,
        std::vector<uint8_t>& out_bytes,
        const Options& options = Options{}) {

        out_bytes.clear();

        // 1. Locate and consolidate code (.text, .text.*, __text)
        std::vector<uint8_t> code;
        std::vector<uint64_t> section_text_offsets(obj.sections().size(), 0);
        for (size_t i = 0; i < obj.sections().size(); ++i) {
            const auto& sec = obj.sections()[i];
            if (sec.is_code() || sec.name == ".text" || sec.name == "__text" || sec.name.starts_with(".text.")) {
                while ((code.size() % 16) != 0) {
                    if (options.cpu_type == CPU_TYPE_ARM64_VAL) {
                        code.insert(code.end(), { 0x1F, 0x20, 0x03, 0xD5 }); // nop
                    } else {
                        code.push_back(0x90);
                    }
                }
                section_text_offsets[i] = code.size();
                code.insert(code.end(), sec.bytes.begin(), sec.bytes.end());
            }
        }
        if (code.empty()) {
            // Minimal RET if no code present
            if (options.cpu_type == CPU_TYPE_ARM64_VAL) {
                const uint32_t ret = 0xD65F03C0u;
                code.resize(4);
                std::memcpy(code.data(), &ret, 4);
            } else {
                code.push_back(0xC3);
            }
        }

        // Synthesize Sovereign In-Binary Runtime Intrinsics (Zero External Dependencies)
        struct SynthesizedIntrinsic {
            std::string name;
            uint64_t text_offset;
        };
        std::vector<SynthesizedIntrinsic> synthesized_intrinsics;

        if (options.sovereign) {
            for (const auto& rel : obj.relocations()) {
                if (rel.symbol_name.empty()) continue;
                std::string sym = rel.symbol_name;

                const auto* existing_sym = obj.find_symbol(sym);
                if (existing_sym && existing_sym->is_defined()) continue;

                bool already_done = false;
                for (const auto& s : synthesized_intrinsics) {
                    if (s.name == sym) { already_done = true; break; }
                }
                if (already_done) continue;

                std::vector<uint8_t> instr_bytes;
                if (options.cpu_type == CPU_TYPE_ARM64_VAL) {
                    instr_bytes = get_sovereign_intrinsic_arm64(sym);
                } else {
                    instr_bytes = get_sovereign_intrinsic_x64(sym);
                }
                if (instr_bytes.empty()) {
                    bool is_intrinsic = sym.starts_with("__dgtl_rt_") || sym.starts_with("dgtl_rt_") ||
                                        sym == "memcpy" || sym == "memset" || sym == "memcmp" || sym == "memmove" ||
                                        sym == "__memcpy" || sym == "__memset" || sym == "__memcmp" || sym == "__memmove" ||
                                        sym == "__chkstk" || sym == "_chkstk" || sym == "___chkstk_ms" || sym == "__chkstk_ms" ||
                                        sym == "_alloca" || sym == "__alloca";
                    if (!is_intrinsic) continue;
                }
                if (instr_bytes.empty()) {
                    if (options.cpu_type == CPU_TYPE_ARM64_VAL) {
                        instr_bytes = { 0x00, 0x00, 0x80, 0xD2, 0xC0, 0x03, 0x5F, 0xD6 }; // mov x0, 0; ret
                    } else {
                        instr_bytes = { 0x31, 0xC0, 0xC3 }; // xor eax, eax; ret
                    }
                }

                while ((code.size() % 16) != 0) {
                    if (options.cpu_type == CPU_TYPE_ARM64_VAL) {
                        code.insert(code.end(), { 0x1F, 0x20, 0x03, 0xD5 }); // nop
                    } else {
                        code.push_back(0x90);
                    }
                }
                const uint64_t off = code.size();
                code.insert(code.end(), instr_bytes.begin(), instr_bytes.end());
                synthesized_intrinsics.push_back({sym, off});
            }
        }

        // 2. Locate and consolidate data with strict W^X separation (__DATA_CONST & __DATA)
        std::vector<uint8_t> const_data;
        std::vector<uint8_t> mut_data;
        struct SecDataOff {
            uint32_t sec_index;
            std::string name;
            bool is_const;
            uint64_t offset;
        };
        std::vector<SecDataOff> data_offsets;
        uint64_t total_bss_size = 0;

        for (size_t i = 0; i < obj.sections().size(); ++i) {
            const auto& sec = obj.sections()[i];
            if (sec.is_code() || sec.name == ".text" || sec.name == "__text" || sec.name.starts_with(".text.")) continue;
            if (sec.is_bss() || sec.name == ".bss" || sec.name == "__bss") {
                total_bss_size += sec.bss_size;
            } else if (!sec.bytes.empty()) {
                const bool is_ro = (sec.name == ".rodata" || sec.name.starts_with(".rodata.") ||
                                    sec.name == ".rdata" || sec.name.starts_with(".rdata.") ||
                                    sec.name == "__const" || sec.name == "__cstring" ||
                                    (!sec.is_writable() && !sec.is_bss()));
                if (is_ro) {
                    while ((const_data.size() % 16) != 0) const_data.push_back(0);
                    data_offsets.push_back({static_cast<uint32_t>(i), sec.name, true, const_data.size()});
                    const_data.insert(const_data.end(), sec.bytes.begin(), sec.bytes.end());
                } else {
                    while ((mut_data.size() % 16) != 0) mut_data.push_back(0);
                    data_offsets.push_back({static_cast<uint32_t>(i), sec.name, false, mut_data.size()});
                    mut_data.insert(mut_data.end(), sec.bytes.begin(), sec.bytes.end());
                }
            }
        }

        const uint64_t kPageSize = options.page_size;
        const uint64_t kHeadersSize = kPageSize; // Headers padded to page boundary

        // Compute segment offsets and virtual addresses
        // __TEXT segment (headers + code)
        const uint64_t text_file_off = kHeadersSize;
        const uint64_t text_vmaddr   = options.image_base;
        const uint64_t text_vmsize   = ((kHeadersSize + code.size() + (kPageSize - 1)) & ~(kPageSize - 1));
        const uint64_t text_filesize = text_vmsize;

        // __DATA_CONST segment (read-only constants & strings)
        const bool has_const_data = !const_data.empty();
        const uint64_t const_data_file_off = text_filesize;
        const uint64_t const_data_vmaddr   = text_vmaddr + text_vmsize;
        const uint64_t const_data_filesize = has_const_data ? ((const_data.size() + (kPageSize - 1)) & ~(kPageSize - 1)) : 0;
        const uint64_t const_data_vmsize   = const_data_filesize;

        // __DATA segment (mutable data and BSS)
        const bool has_mut_data = (!mut_data.empty() || total_bss_size > 0);
        const uint64_t mut_data_file_off = has_const_data ? (const_data_file_off + const_data_filesize) : text_filesize;
        const uint64_t mut_data_vmaddr   = has_const_data ? (const_data_vmaddr + const_data_vmsize) : (text_vmaddr + text_vmsize);
        const uint64_t mut_data_filesize = has_mut_data ? ((mut_data.size() + (kPageSize - 1)) & ~(kPageSize - 1)) : 0;
        const uint64_t mut_data_vmsize   = has_mut_data ? ((mut_data.size() + total_bss_size + (kPageSize - 1)) & ~(kPageSize - 1)) : 0;

        // __LINKEDIT segment (symbol table, string table, code signature)
        const uint64_t linkedit_file_off = has_mut_data ? (mut_data_file_off + mut_data_filesize) : (has_const_data ? (const_data_file_off + const_data_filesize) : text_filesize);
        const uint64_t linkedit_vmaddr   = has_mut_data ? (mut_data_vmaddr + mut_data_vmsize) : (has_const_data ? (const_data_vmaddr + const_data_vmsize) : (text_vmaddr + text_vmsize));

        // Entry point offset within __text
        uint64_t entry_offset = 0;
        auto find_entry = [&](const std::string& name) -> const dgtl::object::Symbol* {
            return obj.find_symbol(name);
        };
        const dgtl::object::Symbol* entry_sym = find_entry(options.entry_symbol);
        if (!entry_sym) entry_sym = find_entry("_" + options.entry_symbol);
        if (!entry_sym) entry_sym = find_entry("DGTL_kernel_entry");
        if (!entry_sym) entry_sym = find_entry("_DGTL_kernel_entry");
        if (!entry_sym) entry_sym = find_entry("main");
        if (!entry_sym) entry_sym = find_entry("_main");

        if (entry_sym) {
            if (entry_sym->section_index < section_text_offsets.size()) {
                entry_offset = section_text_offsets[entry_sym->section_index] + entry_sym->value;
            } else {
                entry_offset = entry_sym->value;
            }
        }

        // Map data symbols to exact virtual addresses
        std::unordered_map<std::string, uint64_t> data_symbol_addresses;
        for (const auto& sym : obj.symbols()) {
            if (!sym.is_defined() || sym.section_index >= obj.sections().size()) continue;
            const auto& sec = obj.sections()[sym.section_index];
            if (sec.is_code() || sec.name == ".text" || sec.name == "__text" || sec.name.starts_with(".text.")) continue;
            for (const auto& item : data_offsets) {
                if (item.sec_index == sym.section_index || item.name == sec.name) {
                    const uint64_t base_addr = item.is_const ? const_data_vmaddr : mut_data_vmaddr;
                    data_symbol_addresses[sym.name] = base_addr + item.offset + sym.value;
                    break;
                }
            }
        }

        // Apply relocations
        for (const auto& rel : obj.relocations()) {
            if (rel.symbol_name.empty()) continue;
            uint64_t target_addr = 0;

            auto it_data = data_symbol_addresses.find(rel.symbol_name);
            if (it_data != data_symbol_addresses.end()) {
                target_addr = it_data->second;
            } else if (const auto* target_sym = obj.find_symbol(rel.symbol_name); target_sym && target_sym->is_defined()) {
                if (target_sym->section_index < obj.sections().size()) {
                    const auto& sec = obj.sections()[target_sym->section_index];
                    if (sec.is_code() || sec.name == ".text" || sec.name == "__text" || sec.name.starts_with(".text.")) {
                        target_addr = text_vmaddr + text_file_off + section_text_offsets[target_sym->section_index] + target_sym->value;
                    } else {
                        for (const auto& item : data_offsets) {
                            if (item.sec_index == target_sym->section_index || item.name == sec.name) {
                                const uint64_t base_addr = item.is_const ? const_data_vmaddr : mut_data_vmaddr;
                                target_addr = base_addr + item.offset + target_sym->value;
                                break;
                            }
                        }
                    }
                } else {
                    target_addr = text_vmaddr + text_file_off + target_sym->value;
                }
            } else {
                for (const auto& synth : synthesized_intrinsics) {
                    if (synth.name == rel.symbol_name) {
                        target_addr = text_vmaddr + text_file_off + synth.text_offset;
                        break;
                    }
                }
                if (target_addr == 0) {
                    // Try alias stripping (e.g. M__abs -> abs or _abs)
                    auto pos = rel.symbol_name.rfind("__");
                    if (pos != std::string::npos && pos + 2 < rel.symbol_name.size()) {
                        std::string bare = rel.symbol_name.substr(pos + 2);
                        if (const auto* b_sym = obj.find_symbol(bare); b_sym && b_sym->is_defined()) {
                            target_addr = text_vmaddr + text_file_off + b_sym->value;
                        } else if (const auto* u_sym = obj.find_symbol("_" + bare); u_sym && u_sym->is_defined()) {
                            target_addr = text_vmaddr + text_file_off + u_sym->value;
                        } else {
                            for (const auto& synth : synthesized_intrinsics) {
                                if (synth.name == bare || synth.name == ("_" + bare)) {
                                    target_addr = text_vmaddr + text_file_off + synth.text_offset;
                                    break;
                                }
                            }
                        }
                    }
                }
                if (target_addr == 0) {
                    std::string_view stripped = rel.symbol_name;
                    if (stripped.starts_with("__dgtl_rt_")) stripped.remove_prefix(10);
                    else if (stripped.starts_with("dgtl_rt_")) stripped.remove_prefix(8);
                    else if (stripped.starts_with("__")) stripped.remove_prefix(2);
                    else if (stripped.starts_with("_")) stripped.remove_prefix(1);
                    if (stripped != rel.symbol_name) {
                        std::string candidates[] = {
                            std::string(stripped),
                            "_" + std::string(stripped),
                            "__" + std::string(stripped),
                            "__dgtl_rt_" + std::string(stripped),
                            "dgtl_rt_" + std::string(stripped)
                        };
                        for (const auto& cand : candidates) {
                            if (const auto* c_sym = obj.find_symbol(cand); c_sym && c_sym->is_defined()) {
                                target_addr = text_vmaddr + text_file_off + c_sym->value;
                                break;
                            }
                            for (const auto& synth : synthesized_intrinsics) {
                                if (synth.name == cand) {
                                    target_addr = text_vmaddr + text_file_off + synth.text_offset;
                                    break;
                                }
                            }
                            if (target_addr != 0) break;
                        }
                    }
                }
                if (target_addr == 0) {
                    uint64_t builtin_addr = 0;
                    if (dgtl::linker::SymbolResolver::resolve_builtin_runtime_symbol(rel.symbol_name, builtin_addr)) {
                        target_addr = builtin_addr;
                    }
                }
                if (target_addr == 0 && (rel.symbol_name.starts_with("__str_") || rel.symbol_name.starts_with(".rdata") || rel.symbol_name.starts_with("__const"))) {
                    uint64_t eff_val = 0;
                    if (rel.symbol_name.starts_with("__str_")) {
                        try {
                            eff_val = std::stoull(rel.symbol_name.substr(6));
                        } catch (...) {}
                    }
                    if (has_const_data) {
                        target_addr = const_data_vmaddr + eff_val;
                    } else if (has_mut_data) {
                        target_addr = mut_data_vmaddr + eff_val;
                    }
                }
            }

            const uint64_t rel_code_offset = (rel.section_index < section_text_offsets.size())
                ? (section_text_offsets[rel.section_index] + rel.offset)
                : rel.offset;

            if (target_addr != 0) {
                const uint64_t pc = text_vmaddr + text_file_off + rel_code_offset;
                if (rel.kind == dgtl::object::RelocationKind::Call26 || rel.kind == dgtl::object::RelocationKind::Branch26) {
                    if (rel_code_offset + 4 <= code.size()) {
                        dgtl::object::RelocationApplier::apply_branch26_arm64(code.data() + rel_code_offset, pc, target_addr);
                    }
                } else if (rel.kind == dgtl::object::RelocationKind::PCRel32) {
                    if (rel_code_offset + 4 <= code.size()) {
                        dgtl::object::RelocationApplier::apply_pc32(code.data() + rel_code_offset, pc, target_addr, rel.addend);
                    }
                }
            } else {
                if (rel.kind == dgtl::object::RelocationKind::PCRel32) {
                    if (rel_code_offset > 0 && rel_code_offset + 4 <= code.size() && code[rel_code_offset - 1] == 0xE8) {
                        code[rel_code_offset - 1] = 0x0F;
                        code[rel_code_offset]     = 0x1F;
                        code[rel_code_offset + 1] = 0x44;
                        code[rel_code_offset + 2] = 0x00;
                        code[rel_code_offset + 3] = 0x00;
                    }
                } else if (rel.kind == dgtl::object::RelocationKind::Call26 || rel.kind == dgtl::object::RelocationKind::Branch26) {
                    if (rel_code_offset + 4 <= code.size()) {
                        code[rel_code_offset]     = 0x1F;
                        code[rel_code_offset + 1] = 0x20;
                        code[rel_code_offset + 2] = 0x03;
                        code[rel_code_offset + 3] = 0xD5;
                    }
                }
            }
        }

        // 3. Build Symbol Table and String Table for LC_SYMTAB
        std::vector<Nlist64> symtab;
        std::vector<char> strtab;
        strtab.push_back('\0'); // Index 0 is always null byte

        auto add_string = [&](std::string_view s) -> uint32_t {
            if (s.empty()) return 0;
            const uint32_t off = static_cast<uint32_t>(strtab.size());
            strtab.insert(strtab.end(), s.begin(), s.end());
            strtab.push_back('\0');
            return off;
        };

        const uint8_t text_sect_id  = 1;
        const uint8_t const_sect_id = has_const_data ? 2 : 0;
        const uint8_t data_sect_id  = has_mut_data ? (has_const_data ? 3 : 2) : 0;

        for (const auto& sym : obj.symbols()) {
            if (!sym.is_defined() || sym.name.empty()) continue;

            Nlist64 nl{};
            nl.n_strx = add_string(sym.name);
            nl.n_type = N_SECT_TYPE | (sym.is_global() ? N_EXT_FLAG : 0);
            nl.n_desc = 0;

            if (sym.section_index < obj.sections().size()) {
                const auto& sec = obj.sections()[sym.section_index];
                if (sec.is_code() || sec.name == ".text" || sec.name == "__text" || sec.name.starts_with(".text.")) {
                    nl.n_sect  = text_sect_id;
                    nl.n_value = text_vmaddr + text_file_off + section_text_offsets[sym.section_index] + sym.value;
                } else {
                    bool found = false;
                    for (const auto& item : data_offsets) {
                        if (item.sec_index == sym.section_index || item.name == sec.name) {
                            nl.n_sect  = item.is_const ? const_sect_id : data_sect_id;
                            const uint64_t base_addr = item.is_const ? const_data_vmaddr : mut_data_vmaddr;
                            nl.n_value = base_addr + item.offset + sym.value;
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        nl.n_sect  = data_sect_id ? data_sect_id : text_sect_id;
                        nl.n_value = mut_data_vmaddr + sym.value;
                    }
                }
            } else {
                nl.n_sect  = text_sect_id;
                nl.n_value = text_vmaddr + text_file_off + sym.value;
            }
            symtab.push_back(nl);
        }

        for (const auto& synth : synthesized_intrinsics) {
            Nlist64 nl{};
            nl.n_strx = add_string(synth.name);
            nl.n_type = N_SECT_TYPE | N_EXT_FLAG;
            nl.n_sect = text_sect_id;
            nl.n_desc = 0;
            nl.n_value = text_vmaddr + text_file_off + synth.text_offset;
            symtab.push_back(nl);
        }

        while ((strtab.size() % 16) != 0) strtab.push_back('\0');

        // 4. Prepare Load Commands
        const uint32_t seg_pagezero_size = sizeof(SegmentCommand64);
        const uint32_t seg_text_size = sizeof(SegmentCommand64) + sizeof(Section64);
        const uint32_t seg_const_data_size = has_const_data ? (sizeof(SegmentCommand64) + sizeof(Section64)) : 0;
        const uint32_t seg_data_size = has_mut_data ? (sizeof(SegmentCommand64) + sizeof(Section64)) : 0;
        const uint32_t seg_linkedit_size = sizeof(SegmentCommand64);

        const uint32_t dylinker_name_padded = ((static_cast<uint32_t>(options.dylinker.size() + 1) + 7) & ~7);
        const uint32_t lc_dylinker_size = sizeof(DylinkerCommand) + dylinker_name_padded;

        const uint32_t dylib_name_padded = ((static_cast<uint32_t>(options.default_dylib.size() + 1) + 7) & ~7);
        const uint32_t lc_dylib_size = sizeof(DylibCommand) + dylib_name_padded;

        const uint32_t lc_main_size = sizeof(EntrypointCommand);
        const uint32_t lc_symtab_size = sizeof(SymtabCommand);
        const uint32_t lc_codesign_size = options.emit_ad_hoc_signature ? sizeof(LinkeditDataCommand) : 0;

        const uint32_t ncmds = 6 + (has_const_data ? 1 : 0) + (has_mut_data ? 1 : 0) + (options.emit_ad_hoc_signature ? 1 : 0);
        const uint32_t sizeofcmds = seg_pagezero_size + seg_text_size + seg_const_data_size + seg_data_size + seg_linkedit_size +
                                    lc_dylinker_size + lc_dylib_size + lc_main_size + lc_symtab_size + lc_codesign_size;

        const uint64_t symoff  = linkedit_file_off;
        const uint32_t nsyms   = static_cast<uint32_t>(symtab.size());
        const uint64_t stroff  = symoff + nsyms * sizeof(Nlist64);
        const uint32_t strsize = static_cast<uint32_t>(strtab.size());
        const uint64_t linkedit_table_end = stroff + strsize;

        // Reserve executable buffer for headers, code, data, and symtab
        const uint64_t initial_file_size = linkedit_table_end;
        out_bytes.assign(initial_file_size, 0);

        auto write_u32 = [&](size_t off, uint32_t val) {
            for (int i = 0; i < 4; ++i) out_bytes[off + i] = static_cast<uint8_t>((val >> (i * 8)) & 0xFF);
        };
        auto write_u64 = [&](size_t off, uint64_t val) {
            for (int i = 0; i < 8; ++i) out_bytes[off + i] = static_cast<uint8_t>((val >> (i * 8)) & 0xFF);
        };
        auto write_string_padded = [&](size_t off, std::string_view str, size_t total_len) {
            std::memcpy(out_bytes.data() + off, str.data(), str.size());
            std::memset(out_bytes.data() + off + str.size(), 0, total_len - str.size());
        };

        // 1. Mach-O Header (32 bytes)
        write_u32(0,  MH_MAGIC_64_VAL);
        write_u32(4,  static_cast<uint32_t>(options.cpu_type));
        write_u32(8,  static_cast<uint32_t>(options.cpu_subtype));
        write_u32(12, MH_EXECUTE_TYPE);
        write_u32(16, ncmds);
        write_u32(20, sizeofcmds);
        write_u32(24, MH_NOUNDEFS_FLAG | MH_DYLDLINK_FLAG | MH_TWOLEVEL_FLAG | MH_PIE_FLAG);
        write_u32(28, 0); // reserved

        size_t cmd_off = sizeof(Header64);

        // Command 1: __PAGEZERO (4GB unmapped guard)
        write_u32(cmd_off + 0,  LC_SEGMENT_64_CMD);
        write_u32(cmd_off + 4,  seg_pagezero_size);
        write_string_padded(cmd_off + 8, "__PAGEZERO", 16);
        write_u64(cmd_off + 24, 0);                 // vmaddr
        write_u64(cmd_off + 32, 0x100000000ULL);    // vmsize = 4GB
        write_u64(cmd_off + 40, 0);                 // fileoff
        write_u64(cmd_off + 48, 0);                 // filesize
        write_u32(cmd_off + 56, 0);                 // maxprot = 0
        write_u32(cmd_off + 60, 0);                 // initprot = 0
        write_u32(cmd_off + 64, 0);                 // nsects = 0
        write_u32(cmd_off + 68, 0);                 // flags = 0
        cmd_off += seg_pagezero_size;

        // Command 2: __TEXT (headers + code)
        write_u32(cmd_off + 0,  LC_SEGMENT_64_CMD);
        write_u32(cmd_off + 4,  seg_text_size);
        write_string_padded(cmd_off + 8, "__TEXT", 16);
        write_u64(cmd_off + 24, text_vmaddr);
        write_u64(cmd_off + 32, text_vmsize);
        write_u64(cmd_off + 40, 0); // fileoff includes headers
        write_u64(cmd_off + 48, text_filesize);
        write_u32(cmd_off + 56, 5); // maxprot = R+X
        write_u32(cmd_off + 60, 5); // initprot = R+X
        write_u32(cmd_off + 64, 1); // nsects = 1 (__text)
        write_u32(cmd_off + 68, 0); // flags

        // Section __TEXT,__text
        const size_t sec_text_off = cmd_off + sizeof(SegmentCommand64);
        write_string_padded(sec_text_off + 0,  "__text", 16);
        write_string_padded(sec_text_off + 16, "__TEXT", 16);
        write_u64(sec_text_off + 32, text_vmaddr + text_file_off); // addr
        write_u64(sec_text_off + 40, code.size());                // size
        write_u32(sec_text_off + 48, static_cast<uint32_t>(text_file_off)); // offset
        write_u32(sec_text_off + 52, 4);                          // align = 16 bytes (2^4)
        write_u32(sec_text_off + 56, 0);                          // reloff
        write_u32(sec_text_off + 60, 0);                          // nreloc
        write_u32(sec_text_off + 64, S_ATTR_PURE_INSTRUCTIONS | S_ATTR_SOME_INSTRUCTIONS);
        cmd_off += seg_text_size;

        // Command 3: __DATA_CONST (read-only constants/strings)
        if (has_const_data) {
            write_u32(cmd_off + 0,  LC_SEGMENT_64_CMD);
            write_u32(cmd_off + 4,  seg_const_data_size);
            write_string_padded(cmd_off + 8, "__DATA_CONST", 16);
            write_u64(cmd_off + 24, const_data_vmaddr);
            write_u64(cmd_off + 32, const_data_vmsize);
            write_u64(cmd_off + 40, const_data_file_off);
            write_u64(cmd_off + 48, const_data_filesize);
            write_u32(cmd_off + 56, 3); // maxprot = R
            write_u32(cmd_off + 60, 1); // initprot = R
            write_u32(cmd_off + 64, 1); // nsects = 1 (__const)
            write_u32(cmd_off + 68, 0);

            const size_t sec_const_off = cmd_off + sizeof(SegmentCommand64);
            write_string_padded(sec_const_off + 0,  "__const", 16);
            write_string_padded(sec_const_off + 16, "__DATA_CONST", 16);
            write_u64(sec_const_off + 32, const_data_vmaddr);
            write_u64(sec_const_off + 40, const_data.size());
            write_u32(sec_const_off + 48, static_cast<uint32_t>(const_data_file_off));
            write_u32(sec_const_off + 52, 4);
            write_u32(sec_const_off + 56, 0);
            write_u32(sec_const_off + 60, 0);
            write_u32(sec_const_off + 64, 0);
            cmd_off += seg_const_data_size;
        }

        // Command 4: __DATA (mutable data and BSS)
        if (has_mut_data) {
            write_u32(cmd_off + 0,  LC_SEGMENT_64_CMD);
            write_u32(cmd_off + 4,  seg_data_size);
            write_string_padded(cmd_off + 8, "__DATA", 16);
            write_u64(cmd_off + 24, mut_data_vmaddr);
            write_u64(cmd_off + 32, mut_data_vmsize);
            write_u64(cmd_off + 40, mut_data_file_off);
            write_u64(cmd_off + 48, mut_data_filesize);
            write_u32(cmd_off + 56, 3); // maxprot = R+W
            write_u32(cmd_off + 60, 3); // initprot = R+W
            write_u32(cmd_off + 64, 1); // nsects = 1 (__data)
            write_u32(cmd_off + 68, 0);

            const size_t sec_data_off = cmd_off + sizeof(SegmentCommand64);
            write_string_padded(sec_data_off + 0,  "__data", 16);
            write_string_padded(sec_data_off + 16, "__DATA", 16);
            write_u64(sec_data_off + 32, mut_data_vmaddr);
            write_u64(sec_data_off + 40, mut_data.size());
            write_u32(sec_data_off + 48, static_cast<uint32_t>(mut_data_file_off));
            write_u32(sec_data_off + 52, 4);
            write_u32(sec_data_off + 56, 0);
            write_u32(sec_data_off + 60, 0);
            write_u32(sec_data_off + 64, 0);
            cmd_off += seg_data_size;
        }

        // Command 5: __LINKEDIT
        const size_t linkedit_cmd_off = cmd_off;
        write_u32(cmd_off + 0,  LC_SEGMENT_64_CMD);
        write_u32(cmd_off + 4,  seg_linkedit_size);
        write_string_padded(cmd_off + 8, "__LINKEDIT", 16);
        write_u64(cmd_off + 24, linkedit_vmaddr);
        write_u64(cmd_off + 32, kPageSize); // Placeholder vmsize
        write_u64(cmd_off + 40, linkedit_file_off);
        write_u64(cmd_off + 48, 0);         // Placeholder filesize
        write_u32(cmd_off + 56, 1);         // maxprot = R
        write_u32(cmd_off + 60, 1);         // initprot = R
        write_u32(cmd_off + 64, 0);         // nsects = 0
        write_u32(cmd_off + 68, 0);
        cmd_off += seg_linkedit_size;

        // Command 6: LC_LOAD_DYLINKER
        write_u32(cmd_off + 0, LC_LOAD_DYLINKER_CMD);
        write_u32(cmd_off + 4, lc_dylinker_size);
        write_u32(cmd_off + 8, sizeof(DylinkerCommand)); // name offset
        write_string_padded(cmd_off + sizeof(DylinkerCommand), options.dylinker, dylinker_name_padded);
        cmd_off += lc_dylinker_size;

        // Command 7: LC_LOAD_DYLIB
        write_u32(cmd_off + 0,  LC_LOAD_DYLIB_CMD);
        write_u32(cmd_off + 4,  lc_dylib_size);
        write_u32(cmd_off + 8,  sizeof(DylibCommand));
        write_u32(cmd_off + 12, 0); // timestamp
        write_u32(cmd_off + 16, 0x010000); // current_version 1.0.0
        write_u32(cmd_off + 20, 0x010000); // compatibility_version 1.0.0
        write_string_padded(cmd_off + sizeof(DylibCommand), options.default_dylib, dylib_name_padded);
        cmd_off += lc_dylib_size;

        // Command 8: LC_MAIN
        write_u32(cmd_off + 0,  LC_MAIN_CMD);
        write_u32(cmd_off + 4,  lc_main_size);
        write_u64(cmd_off + 8,  text_file_off + entry_offset); // entryoff
        write_u64(cmd_off + 16, 0);                            // stacksize
        cmd_off += lc_main_size;

        // Command 9: LC_SYMTAB
        const size_t symtab_cmd_off = cmd_off;
        write_u32(cmd_off + 0,  LC_SYMTAB_CMD);
        write_u32(cmd_off + 4,  lc_symtab_size);
        write_u32(cmd_off + 8,  static_cast<uint32_t>(symoff));  // symoff
        write_u32(cmd_off + 12, nsyms);                         // nsyms
        write_u32(cmd_off + 16, static_cast<uint32_t>(stroff));  // stroff
        write_u32(cmd_off + 20, strsize);                        // strsize
        cmd_off += lc_symtab_size;

        // Command 10: LC_CODE_SIGNATURE (optional)
        size_t codesign_cmd_off = 0;
        if (options.emit_ad_hoc_signature) {
            codesign_cmd_off = cmd_off;
            write_u32(cmd_off + 0, LC_CODE_SIGNATURE_CMD);
            write_u32(cmd_off + 4, lc_codesign_size);
            write_u32(cmd_off + 8, 0); // dataoff (filled after code emission)
            write_u32(cmd_off + 12, 0); // datasize
            cmd_off += lc_codesign_size;
        }

        // Copy code and data into position
        std::copy(code.begin(), code.end(), out_bytes.begin() + text_file_off);
        if (has_const_data && !const_data.empty()) {
            std::copy(const_data.begin(), const_data.end(), out_bytes.begin() + const_data_file_off);
        }
        if (has_mut_data && !mut_data.empty()) {
            std::copy(mut_data.begin(), mut_data.end(), out_bytes.begin() + mut_data_file_off);
        }

        // Copy symbol table and string table into position in __LINKEDIT
        if (!symtab.empty()) {
            std::memcpy(out_bytes.data() + symoff, symtab.data(), symtab.size() * sizeof(Nlist64));
        }
        if (!strtab.empty()) {
            std::memcpy(out_bytes.data() + stroff, strtab.data(), strtab.size());
        }

        // 5. In-Memory Sovereign Code Signing (Apple Silicon Ad-Hoc SuperBlob)
        if (options.emit_ad_hoc_signature) {
            // Align signature start offset to 16 bytes
            while ((out_bytes.size() % 16) != 0) {
                out_bytes.push_back(0);
            }

            const size_t binary_data_size = out_bytes.size();
            const uint32_t page_count = static_cast<uint32_t>((binary_data_size + 4095) / 4096);

            // Compute SHA-256 for each 4096-byte page
            std::vector<uint8_t> page_hashes;
            page_hashes.reserve(page_count * 32);

            for (uint32_t p = 0; p < page_count; ++p) {
                const size_t start = static_cast<size_t>(p) * 4096;
                const size_t len = std::min<size_t>(4096, binary_data_size - start);
                uint8_t h[32];
                sha256::hash(out_bytes.data() + start, len, h);
                page_hashes.insert(page_hashes.end(), h, h + 32);
            }

            // Build CS_CodeDirectory (Big-Endian network order)
            std::vector<uint8_t> cd;
            auto cd_u32be = [&](uint32_t val) {
                cd.push_back(static_cast<uint8_t>((val >> 24) & 0xFF));
                cd.push_back(static_cast<uint8_t>((val >> 16) & 0xFF));
                cd.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
                cd.push_back(static_cast<uint8_t>(val & 0xFF));
            };

            const std::string ident = "a.out";
            const uint32_t cd_header_size = 88;
            const uint32_t ident_offset = cd_header_size;
            const uint32_t hash_offset = ident_offset + static_cast<uint32_t>(ident.size() + 1);
            const uint32_t cd_length = hash_offset + static_cast<uint32_t>(page_hashes.size());

            cd_u32be(0xFADE0C02u); // CSMAGIC_CODEDIRECTORY
            cd_u32be(cd_length);
            cd_u32be(0x00020400u); // version 2.4
            cd_u32be(0x00020002u); // flags: CS_ADHOC | CS_LINKER_SIGNED
            cd_u32be(hash_offset);
            cd_u32be(ident_offset);
            cd_u32be(0);           // nSpecialSlots = 0
            cd_u32be(page_count);   // nCodeSlots
            cd_u32be(static_cast<uint32_t>(binary_data_size)); // codeLimit
            cd.push_back(32);      // hashSize = 32
            cd.push_back(2);       // hashType = 2 (SHA-256)
            cd.push_back(0);       // platform = 0
            cd.push_back(12);      // pageSize = 2^12 = 4096
            cd_u32be(0);           // spare2
            cd_u32be(0);           // scatterOffset
            cd_u32be(0);           // teamOffset
            cd_u32be(0);           // spare3
            cd_u32be(0);           // codeLimit64

            // Identifier
            cd.insert(cd.end(), ident.begin(), ident.end());
            cd.push_back(0);

            // Hash slots
            cd.insert(cd.end(), page_hashes.begin(), page_hashes.end());

            // Build CS_SuperBlob
            std::vector<uint8_t> superblob;
            auto sb_u32be = [&](uint32_t val) {
                superblob.push_back(static_cast<uint8_t>((val >> 24) & 0xFF));
                superblob.push_back(static_cast<uint8_t>((val >> 16) & 0xFF));
                superblob.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
                superblob.push_back(static_cast<uint8_t>(val & 0xFF));
            };

            const uint32_t superblob_header_size = 12 + 8; // magic, length, count + 1 slot (type, offset)
            const uint32_t total_superblob_len = superblob_header_size + static_cast<uint32_t>(cd.size());

            sb_u32be(0xFADE0CC0u); // CSMAGIC_EMBEDDED_SIGNATURE
            sb_u32be(total_superblob_len);
            sb_u32be(1);           // count = 1
            sb_u32be(0);           // slot type = 0 (CSSLOT_CODEDIRECTORY)
            sb_u32be(superblob_header_size); // offset

            superblob.insert(superblob.end(), cd.begin(), cd.end());

            // Append SuperBlob to out_bytes
            const uint64_t sig_data_off = out_bytes.size();
            out_bytes.insert(out_bytes.end(), superblob.begin(), superblob.end());
            const uint64_t sig_data_size = superblob.size();

            // Align total file size to page boundary
            while ((out_bytes.size() % kPageSize) != 0) {
                out_bytes.push_back(0);
            }

            // Update LC_CODE_SIGNATURE command
            write_u32(codesign_cmd_off + 8,  static_cast<uint32_t>(sig_data_off));
            write_u32(codesign_cmd_off + 12, static_cast<uint32_t>(sig_data_size));

            // Update __LINKEDIT segment command
            const uint64_t linkedit_filesize = out_bytes.size() - linkedit_file_off;
            const uint64_t linkedit_vmsize   = (linkedit_filesize + (kPageSize - 1)) & ~(kPageSize - 1);
            write_u64(linkedit_cmd_off + 32, linkedit_vmsize);
            write_u64(linkedit_cmd_off + 48, linkedit_filesize);
        } else {
            // Align total file size to page boundary
            while ((out_bytes.size() % kPageSize) != 0) {
                out_bytes.push_back(0);
            }
            const uint64_t linkedit_filesize = out_bytes.size() - linkedit_file_off;
            const uint64_t linkedit_vmsize   = (linkedit_filesize + (kPageSize - 1)) & ~(kPageSize - 1);
            write_u64(linkedit_cmd_off + 32, linkedit_vmsize);
            write_u64(linkedit_cmd_off + 48, linkedit_filesize);
        }

        return true;
    }
};

} // namespace dgtl::linker::macho
