/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Component:   Object Section Definition & Operations
 * Standards:   C++26 / Clang 18+ / MSVC 19.40+ / GCC 14+
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

module;

#include <cstdlib>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

export module dgtl.object.section;

export namespace dgtl::object {

enum class SectionKind : std::uint8_t {
    Text,
    Data,
    BSS,
    RData,
    Relocations,
    SymbolTable,
    StringTable,
    Debug,
    TLS,
    ExceptionTable,
    ExceptionInfo,
    Custom
};

enum SectionFlags : std::uint32_t {
    None        = 0x00000000u,
    Code        = 0x00000001u,
    Read        = 0x00000002u,
    Write       = 0x00000004u,
    Executable  = 0x00000008u,
    ZeroFill    = 0x00000010u,
    Alloc       = 0x00000020u,
    ThreadLocal = 0x00000040u,
    Mergeable   = 0x00000080u,
    Strings     = 0x00000100u,
    Discardable = 0x00000200u,
    Shared      = 0x00000400u
};

struct Section {
    std::string name;
    SectionKind kind = SectionKind::Text;
    std::uint32_t flags = SectionFlags::Code | SectionFlags::Read | SectionFlags::Executable;
    std::uint64_t alignment = 16u;
    std::uint64_t address = 0u;
    std::uint64_t bss_size = 0u;
    std::vector<std::uint8_t> bytes;

    Section() = default;
    Section(std::string n, std::uint64_t a = 16u,
            std::uint32_t f = SectionFlags::Code | SectionFlags::Read | SectionFlags::Executable,
            SectionKind k = SectionKind::Text)
        : name(std::move(n)), kind(k), flags(f), alignment(a) {}

    [[nodiscard]] std::size_t size() const noexcept {
        return (kind == SectionKind::BSS) ? static_cast<std::size_t>(bss_size) : bytes.size();
    }

    [[nodiscard]] bool empty() const noexcept {
        return (kind == SectionKind::BSS) ? (bss_size == 0) : bytes.empty();
    }

    [[nodiscard]] bool is_code() const noexcept {
        return (flags & SectionFlags::Code) != 0 || kind == SectionKind::Text;
    }

    [[nodiscard]] bool is_writable() const noexcept {
        return (flags & SectionFlags::Write) != 0;
    }

    [[nodiscard]] bool is_executable() const noexcept {
        return (flags & SectionFlags::Executable) != 0 || is_code();
    }

    [[nodiscard]] bool is_bss() const noexcept {
        return kind == SectionKind::BSS || (flags & SectionFlags::ZeroFill) != 0;
    }

    void emit_u8(std::uint8_t b) {
        bytes.push_back(b);
    }

    void emit_u16(std::uint16_t val) {
        bytes.push_back(static_cast<std::uint8_t>(val & 0xFFu));
        bytes.push_back(static_cast<std::uint8_t>((val >> 8u) & 0xFFu));
    }

    void emit_u32(std::uint32_t val) {
        for (std::size_t i = 0; i < 4; ++i) {
            bytes.push_back(static_cast<std::uint8_t>((val >> (i * 8u)) & 0xFFu));
        }
    }

    void emit_u64(std::uint64_t val) {
        for (std::size_t i = 0; i < 8; ++i) {
            bytes.push_back(static_cast<std::uint8_t>((val >> (i * 8u)) & 0xFFu));
        }
    }

    void append_bytes(const std::uint8_t* data, std::size_t len) {
        if (data && len > 0) {
            bytes.insert(bytes.end(), data, data + len);
        }
    }

    void align_to(std::uint64_t align) {
        if (align <= 1) return;
        const std::uint64_t rem = bytes.size() % align;
        if (rem != 0) {
            const std::uint64_t pad = align - rem;
            bytes.insert(bytes.end(), pad, 0u);
        }
    }
};

} // namespace dgtl::object



