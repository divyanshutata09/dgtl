/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Component:   Object Relocation Model & Resolvers
 * Standards:   C++26 / Clang 18+ / MSVC 19.40+ / GCC 14+
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

module;

#include <cstdlib>
#include <cstdint>
#include <climits>
#include <span>
#include <string>
#include <string_view>
#include <utility>

export module dgtl.object.relocation;

export namespace dgtl::object {

enum class RelocationKind : std::uint16_t {
    None = 0,
    Absolute32,
    Absolute64,
    Relative64,
    PCRel32,
    PCRel64,
    Branch26,
    Call26,
    GOT32,
    GOT64,
    PLT32,
    PLT64,
    TLS,
    High20,
    Low12,
    SecRel32
};

struct RelocationPolicy {
    bool pc_relative = false;
    std::uint8_t width = 4u;
    bool signed_value = true;
};

struct Relocation {
    std::uint64_t offset = 0u;
    std::string symbol_name;
    std::uint32_t symbol_index = 0u;
    std::int64_t addend = 0;
    std::uint32_t type = 0u;
    RelocationKind kind = RelocationKind::PCRel32;
    std::uint32_t section_index = 0u;

    Relocation() = default;
    Relocation(std::uint64_t o, std::string sym, std::int64_t add = 0, std::uint32_t t = 0, RelocationKind k = RelocationKind::PCRel32)
        : offset(o), symbol_name(std::move(sym)), addend(add), type(t), kind(k) {}

    [[nodiscard]] bool is_pc_relative() const noexcept {
        return kind == RelocationKind::PCRel32 || kind == RelocationKind::PCRel64 ||
               kind == RelocationKind::Branch26 || kind == RelocationKind::Call26 ||
               kind == RelocationKind::PLT32;
    }
};

class RelocationApplier {
public:
    static bool apply_pc32(std::uint8_t* target, std::uint64_t pc, std::uint64_t dest, std::int64_t addend = 0) noexcept {
        if (!target) return false;
        const std::int64_t delta = static_cast<std::int64_t>(dest) + addend - static_cast<std::int64_t>(pc);
        if (delta < INT32_MIN || delta > INT32_MAX) return false;
        const auto val = static_cast<std::int32_t>(delta);
        for (std::size_t i = 0; i < 4; ++i) {
            target[i] = static_cast<std::uint8_t>((val >> (i * 8)) & 0xFF);
        }
        return true;
    }

    static bool apply_abs64(std::uint8_t* target, std::uint64_t value) noexcept {
        if (!target) return false;
        for (std::size_t i = 0; i < 8; ++i) {
            target[i] = static_cast<std::uint8_t>((value >> (i * 8)) & 0xFF);
        }
        return true;
    }

    static bool apply_abs32(std::uint8_t* target, std::uint32_t value) noexcept {
        if (!target) return false;
        for (std::size_t i = 0; i < 4; ++i) {
            target[i] = static_cast<std::uint8_t>((value >> (i * 8)) & 0xFF);
        }
        return true;
    }

    static bool apply(std::span<std::uint8_t> section_bytes, const Relocation& reloc, std::uint64_t place, std::uint64_t symbol_target_address) noexcept {
        if (reloc.offset >= section_bytes.size()) return false;
        std::uint8_t* target = section_bytes.data() + reloc.offset;
        switch (reloc.kind) {
            case RelocationKind::PCRel32:
                if (reloc.offset + 4 > section_bytes.size()) return false;
                return apply_pc32(target, place, symbol_target_address, reloc.addend);
            case RelocationKind::Absolute64:
                if (reloc.offset + 8 > section_bytes.size()) return false;
                return apply_abs64(target, symbol_target_address + static_cast<std::uint64_t>(reloc.addend));
            case RelocationKind::Absolute32:
                if (reloc.offset + 4 > section_bytes.size()) return false;
                return apply_abs32(target, static_cast<std::uint32_t>(symbol_target_address + reloc.addend));
            default:
                return false;
        }
    }
};

} // namespace dgtl::object



