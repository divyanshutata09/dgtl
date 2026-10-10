/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Component:   WebAssembly (WASM) Relocation Types & Appliers
 * Standards:   C++26 / Clang 18+ / MSVC 19.40+ / GCC 14+
 * Module:      dgtl.linker.wasm.relocations
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

module;

#include <cstdint>
#include <cstddef>
#include <span>
#include <vector>

export module dgtl.linker.wasm.relocations;

export namespace dgtl::linker::wasm {

// ─────────────────────────────────────────────────────────────────────────────
// WASM Relocation Type Constants (Standard WebAssembly Tool Conventions)
// ─────────────────────────────────────────────────────────────────────────────

inline constexpr uint32_t R_WASM_FUNCTION_INDEX_LEB    = 0;
inline constexpr uint32_t R_WASM_TABLE_INDEX_SLEB     = 1;
inline constexpr uint32_t R_WASM_TABLE_INDEX_I32      = 2;
inline constexpr uint32_t R_WASM_MEMORY_ADDR_LEB      = 3;
inline constexpr uint32_t R_WASM_MEMORY_ADDR_SLEB     = 4;
inline constexpr uint32_t R_WASM_MEMORY_ADDR_I32      = 5;
inline constexpr uint32_t R_WASM_TYPE_INDEX_LEB       = 6;
inline constexpr uint32_t R_WASM_GLOBAL_INDEX_LEB     = 7;
inline constexpr uint32_t R_WASM_FUNCTION_OFFSET_I32  = 8;
inline constexpr uint32_t R_WASM_SECTION_OFFSET_I32   = 9;
inline constexpr uint32_t R_WASM_TAG_INDEX_LEB        = 10;
inline constexpr uint32_t R_WASM_MEMORY_ADDR_REL_SLEB = 11;
inline constexpr uint32_t R_WASM_TABLE_INDEX_REL_SLEB = 12;
inline constexpr uint32_t R_WASM_GLOBAL_INDEX_I32     = 13;
inline constexpr uint32_t R_WASM_MEMORY_ADDR_LEB64    = 14;

// ─────────────────────────────────────────────────────────────────────────────
// Relocation In-Place Patcher (Fixed 5-Byte Padding & Standard Patching)
// ─────────────────────────────────────────────────────────────────────────────

class WasmRelocationApplier {
public:
    // Patches a 5-byte fixed-width unsigned LEB128 at the target offset
    static bool patch_u32_leb_5(uint8_t* target, uint32_t val) noexcept {
        if (!target) return false;
        target[0] = static_cast<uint8_t>((val & 0x7F) | 0x80);
        target[1] = static_cast<uint8_t>(((val >> 7) & 0x7F) | 0x80);
        target[2] = static_cast<uint8_t>(((val >> 14) & 0x7F) | 0x80);
        target[3] = static_cast<uint8_t>(((val >> 21) & 0x7F) | 0x80);
        target[4] = static_cast<uint8_t>((val >> 28) & 0x7F);
        return true;
    }

    // Patches a 5-byte fixed-width signed LEB128 at the target offset
    static bool patch_i32_sleb_5(uint8_t* target, int32_t val) noexcept {
        if (!target) return false;
        target[0] = static_cast<uint8_t>((val & 0x7F) | 0x80);
        target[1] = static_cast<uint8_t>(((val >> 7) & 0x7F) | 0x80);
        target[2] = static_cast<uint8_t>(((val >> 14) & 0x7F) | 0x80);
        target[3] = static_cast<uint8_t>(((val >> 21) & 0x7F) | 0x80);
        target[4] = static_cast<uint8_t>((val >> 28) & 0x7F);
        return true;
    }

    // Patches a 4-byte little-endian 32-bit integer at target offset
    static bool patch_i32_le(uint8_t* target, uint32_t val) noexcept {
        if (!target) return false;
        target[0] = static_cast<uint8_t>(val & 0xFF);
        target[1] = static_cast<uint8_t>((val >> 8) & 0xFF);
        target[2] = static_cast<uint8_t>((val >> 16) & 0xFF);
        target[3] = static_cast<uint8_t>((val >> 24) & 0xFF);
        return true;
    }
};

} // namespace dgtl::linker::wasm
