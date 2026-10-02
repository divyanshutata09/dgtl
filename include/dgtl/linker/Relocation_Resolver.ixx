/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

module;

#include <cstdint>
#include <climits>
#include <cstddef>
#include <span>

export module dgtl.linker.relocations;

import dgtl.object.relocation;

export namespace dgtl::linker {

class RelocationResolver {
public:
    static bool apply_pc32(uint8_t* p, uint64_t place, uint64_t target, int64_t addend = 0) noexcept {
        if (!p) return false;
        auto v = static_cast<int64_t>(target) + addend - static_cast<int64_t>(place);
        if (v < INT32_MIN || v > INT32_MAX) return false;
        auto x = static_cast<int32_t>(v);
        for (int i = 0; i < 4; ++i) {
            p[i] = static_cast<uint8_t>(x >> (i * 8));
        }
        return true;
    }

    static bool apply_abs64(uint8_t* p, uint64_t v) noexcept {
        if (!p) return false;
        for (int i = 0; i < 8; ++i) {
            p[i] = static_cast<uint8_t>(v >> (i * 8));
        }
        return true;
    }

    static bool apply_abs32(uint8_t* p, uint32_t v) noexcept {
        if (!p) return false;
        for (int i = 0; i < 4; ++i) {
            p[i] = static_cast<uint8_t>(v >> (i * 8));
        }
        return true;
    }

    static bool apply_branch26_arm64(uint8_t* p, uint64_t place, uint64_t target) noexcept {
        if (!p) return false;
        int64_t offset = static_cast<int64_t>(target) - static_cast<int64_t>(place);
        if ((offset & 3) != 0) return false;
        int64_t imm26 = offset >> 2;
        if (imm26 < -(1 << 25) || imm26 >= (1 << 25)) return false;
        uint32_t insn = static_cast<uint32_t>(p[0]) |
                        (static_cast<uint32_t>(p[1]) << 8) |
                        (static_cast<uint32_t>(p[2]) << 16) |
                        (static_cast<uint32_t>(p[3]) << 24);
        insn = (insn & 0xFC000000u) | (static_cast<uint32_t>(imm26) & 0x03FFFFFFu);
        for (int i = 0; i < 4; ++i) {
            p[i] = static_cast<uint8_t>(insn >> (i * 8));
        }
        return true;
    }

    static bool apply_object_relocation(
        std::span<uint8_t> section_bytes,
        const dgtl::object::Relocation& reloc,
        uint64_t section_address,
        uint64_t symbol_target_address) noexcept {
        if (reloc.offset >= section_bytes.size()) return false;
        uint64_t place = section_address + reloc.offset;
        uint8_t* p = section_bytes.data() + reloc.offset;

        switch (reloc.kind) {
            case dgtl::object::RelocationKind::PCRel32:
                return apply_pc32(p, place, symbol_target_address, reloc.addend);
            case dgtl::object::RelocationKind::Absolute64:
                return apply_abs64(p, symbol_target_address + static_cast<uint64_t>(reloc.addend));
            case dgtl::object::RelocationKind::Absolute32:
                return apply_abs32(p, static_cast<uint32_t>(symbol_target_address + reloc.addend));
            case dgtl::object::RelocationKind::Branch26:
            case dgtl::object::RelocationKind::Call26:
                return apply_branch26_arm64(p, place, symbol_target_address);
            default:
                return dgtl::object::RelocationApplier::apply(
                    section_bytes, reloc, place, symbol_target_address);
        }
    }
};

} // namespace dgtl::linker

export namespace dgtl::backend::linker {
    using namespace dgtl::linker;
} // namespace dgtl::backend::linker



