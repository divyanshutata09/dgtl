/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Component:   WebAssembly (WASM) Binary Specification, Sections & LEB128 Encoders
 * Standards:   C++26 / Clang 18+ / MSVC 19.40+ / GCC 14+
 * Module:      dgtl.linker.wasm.sections
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

module;

#include <cstdint>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>
#include <span>
#include <algorithm>

export module dgtl.linker.wasm.sections;

export namespace dgtl::linker::wasm {

// ─────────────────────────────────────────────────────────────────────────────
// WASM Magic & Binary Specification Constants
// ─────────────────────────────────────────────────────────────────────────────

inline constexpr uint32_t kWasmMagic   = 0x6D736100; // \0asm (little-endian: 0x00, 0x61, 0x73, 0x6D)
inline constexpr uint32_t kWasmVersion = 0x00000001; // Version 1

// Section IDs (WASM Binary Format specification)
inline constexpr uint8_t kSectionCustom    = 0;
inline constexpr uint8_t kSectionType      = 1;
inline constexpr uint8_t kSectionImport    = 2;
inline constexpr uint8_t kSectionFunction  = 3;
inline constexpr uint8_t kSectionTable     = 4;
inline constexpr uint8_t kSectionMemory    = 5;
inline constexpr uint8_t kSectionGlobal    = 6;
inline constexpr uint8_t kSectionExport    = 7;
inline constexpr uint8_t kSectionStart     = 8;
inline constexpr uint8_t kSectionElement   = 9;
inline constexpr uint8_t kSectionCode      = 10;
inline constexpr uint8_t kSectionData      = 11;
inline constexpr uint8_t kSectionDataCount = 12;

// Value Types
inline constexpr uint8_t kValTypeI32       = 0x7F;
inline constexpr uint8_t kValTypeI64       = 0x7E;
inline constexpr uint8_t kValTypeF32       = 0x7D;
inline constexpr uint8_t kValTypeF64       = 0x7C;
inline constexpr uint8_t kValTypeV128      = 0x7B;
inline constexpr uint8_t kValTypeFuncRef   = 0x70;
inline constexpr uint8_t kValTypeExternRef = 0x6F;
inline constexpr uint8_t kFuncTypeTag      = 0x60;

// External / Export Kinds
inline constexpr uint8_t kExternalFunction = 0x00;
inline constexpr uint8_t kExternalTable    = 0x01;
inline constexpr uint8_t kExternalMemory   = 0x02;
inline constexpr uint8_t kExternalGlobal   = 0x03;

// Common WASM Bytecode Instructions
inline constexpr uint8_t kOpcodeUnreachable  = 0x00;
inline constexpr uint8_t kOpcodeNop          = 0x01;
inline constexpr uint8_t kOpcodeBlock        = 0x02;
inline constexpr uint8_t kOpcodeLoop         = 0x03;
inline constexpr uint8_t kOpcodeIf           = 0x04;
inline constexpr uint8_t kOpcodeElse         = 0x05;
inline constexpr uint8_t kOpcodeEnd          = 0x0B;
inline constexpr uint8_t kOpcodeBr           = 0x0C;
inline constexpr uint8_t kOpcodeBrIf         = 0x0D;
inline constexpr uint8_t kOpcodeReturn       = 0x0F;
inline constexpr uint8_t kOpcodeCall         = 0x10;
inline constexpr uint8_t kOpcodeCallIndirect = 0x11;
inline constexpr uint8_t kOpcodeDrop         = 0x1A;
inline constexpr uint8_t kOpcodeLocalGet     = 0x20;
inline constexpr uint8_t kOpcodeLocalSet     = 0x21;
inline constexpr uint8_t kOpcodeLocalTee     = 0x22;
inline constexpr uint8_t kOpcodeGlobalGet    = 0x23;
inline constexpr uint8_t kOpcodeGlobalSet    = 0x24;
inline constexpr uint8_t kOpcodeI32Load      = 0x28;
inline constexpr uint8_t kOpcodeI64Load      = 0x29;
inline constexpr uint8_t kOpcodeI32Store     = 0x36;
inline constexpr uint8_t kOpcodeI64Store     = 0x37;
inline constexpr uint8_t kOpcodeI32Const     = 0x41;
inline constexpr uint8_t kOpcodeI64Const     = 0x42;
inline constexpr uint8_t kOpcodeF32Const     = 0x43;
inline constexpr uint8_t kOpcodeF64Const     = 0x44;
inline constexpr uint8_t kOpcodeI32Add       = 0x6A;
inline constexpr uint8_t kOpcodeI32Sub       = 0x6B;
inline constexpr uint8_t kOpcodeI32Mul       = 0x6C;

// Memory Page Constant (64 KB)
inline constexpr uint32_t kWasmPageBytes = 65536;

// ─────────────────────────────────────────────────────────────────────────────
// LEB128 High-Performance Stream Encoders
// ─────────────────────────────────────────────────────────────────────────────

inline void encode_u32_leb(std::vector<uint8_t>& buf, uint32_t val) noexcept {
    do {
        uint8_t byte = val & 0x7Fu;
        val >>= 7u;
        if (val != 0) byte |= 0x80u;
        buf.push_back(byte);
    } while (val != 0);
}

inline void encode_i32_sleb(std::vector<uint8_t>& buf, int32_t val) noexcept {
    bool more = true;
    while (more) {
        uint8_t byte = static_cast<uint8_t>(val & 0x7F);
        val >>= 7;
        const bool sign_bit = (byte & 0x40) != 0;
        if ((val == 0 && !sign_bit) || (val == -1 && sign_bit)) {
            more = false;
        } else {
            byte |= 0x80;
        }
        buf.push_back(byte);
    }
}

inline void encode_u64_leb(std::vector<uint8_t>& buf, uint64_t val) noexcept {
    do {
        uint8_t byte = val & 0x7Fu;
        val >>= 7u;
        if (val != 0) byte |= 0x80u;
        buf.push_back(byte);
    } while (val != 0);
}

inline void encode_string(std::vector<uint8_t>& buf, std::string_view str) {
    encode_u32_leb(buf, static_cast<uint32_t>(str.size()));
    for (char c : str) {
        buf.push_back(static_cast<uint8_t>(c));
    }
}

inline void encode_section(std::vector<uint8_t>& out, uint8_t sec_id, const std::vector<uint8_t>& payload) {
    out.push_back(sec_id);
    encode_u32_leb(out, static_cast<uint32_t>(payload.size()));
    out.insert(out.end(), payload.begin(), payload.end());
}

// ─────────────────────────────────────────────────────────────────────────────
// WASM Linker Data Structures
// ─────────────────────────────────────────────────────────────────────────────

struct WasmFuncType {
    std::vector<uint8_t> params;
    std::vector<uint8_t> results;

    bool operator==(const WasmFuncType& other) const noexcept {
        return params == other.params && results == other.results;
    }
};

struct WasmImportSpec {
    std::string module_name;
    std::string field_name;
    uint8_t kind = kExternalFunction;
    uint32_t type_index = 0;
};

struct WasmExportSpec {
    std::string name;
    uint8_t kind = kExternalFunction;
    uint32_t index = 0;
};

struct WasmGlobalSpec {
    uint8_t val_type = kValTypeI32;
    bool is_mutable = true;
    int32_t init_i32 = 0;
};

struct WasmDataSegment {
    uint32_t memory_index = 0;
    int32_t offset = 0;
    std::vector<uint8_t> bytes;
};

} // namespace dgtl::linker::wasm
