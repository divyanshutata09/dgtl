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
#include <string_view>

export module dgtl.linker.elf.sections;

import dgtl.object.section;

export namespace dgtl::linker::elf {

struct SectionDescriptor {
    const char* name;
    uint64_t alignment;
    uint32_t flags;
    uint32_t type;
};

inline constexpr uint32_t SHT_PROGBITS = 1;
inline constexpr uint32_t SHT_SYMTAB   = 2;
inline constexpr uint32_t SHT_STRTAB   = 3;
inline constexpr uint32_t SHT_RELA     = 4;
inline constexpr uint32_t SHT_NOBITS   = 8;

inline constexpr uint64_t SHF_WRITE     = 0x1;
inline constexpr uint64_t SHF_ALLOC     = 0x2;
inline constexpr uint64_t SHF_EXECINSTR = 0x4;

} // namespace dgtl::linker::elf

export namespace dgtl::backend::linker::elf {
    using namespace dgtl::linker::elf;
} // namespace dgtl::backend::linker::elf



