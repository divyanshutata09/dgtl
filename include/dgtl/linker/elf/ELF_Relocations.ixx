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

export module dgtl.linker.elf.relocations;

import dgtl.object.relocation;

export namespace dgtl::linker::elf {

inline constexpr uint32_t kRelocNone       = 0;
inline constexpr uint32_t kReloc64         = 1; // R_X86_64_64 / R_AARCH64_ABS64
inline constexpr uint32_t kRelocPC32       = 2; // R_X86_64_PC32
inline constexpr uint32_t kRelocPLT32      = 4; // R_X86_64_PLT32
inline constexpr uint32_t kReloc32         = 10;// R_X86_64_32
inline constexpr uint32_t kRelocBranch26   = 283; // R_AARCH64_JUMP26 / CALL26

[[nodiscard]] inline constexpr uint32_t to_elf_reloc_x86_64(dgtl::object::RelocationKind kind) noexcept {
    switch (kind) {
        case dgtl::object::RelocationKind::Absolute64: return 1;
        case dgtl::object::RelocationKind::PCRel32:    return 2;
        case dgtl::object::RelocationKind::Absolute32: return 10;
        case dgtl::object::RelocationKind::PLT32:
        case dgtl::object::RelocationKind::PLT64:      return 4;
        case dgtl::object::RelocationKind::GOT32:
        case dgtl::object::RelocationKind::GOT64:      return 3;
        default:                                       return 2;
    }
}

} // namespace dgtl::linker::elf

export namespace dgtl::backend::linker::elf {
    using namespace dgtl::linker::elf;
} // namespace dgtl::backend::linker::elf


