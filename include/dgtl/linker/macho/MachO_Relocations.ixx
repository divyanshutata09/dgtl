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

export module dgtl.linker.macho.relocations;

import dgtl.object.relocation;

export namespace dgtl::linker::macho {

inline constexpr uint32_t kRelocVanilla   = 0;
inline constexpr uint32_t kRelocPair      = 1;
inline constexpr uint32_t kRelocBranch26  = 2;
inline constexpr uint32_t kRelocPC32      = 3;
inline constexpr uint32_t kRelocAbsolute64= 4;

// ARM64 Mach-O Relocations
inline constexpr uint32_t ARM64_RELOC_UNSIGNED            = 0;
inline constexpr uint32_t ARM64_RELOC_SUBTRACTOR          = 1;
inline constexpr uint32_t ARM64_RELOC_BRANCH26            = 2;
inline constexpr uint32_t ARM64_RELOC_PAGE21              = 3;
inline constexpr uint32_t ARM64_RELOC_PAGEOFF12           = 4;
inline constexpr uint32_t ARM64_RELOC_GOT_LOAD_PAGE21     = 5;
inline constexpr uint32_t ARM64_RELOC_GOT_LOAD_PAGEOFF12  = 6;
inline constexpr uint32_t ARM64_RELOC_POINTER_TO_GOT      = 7;
inline constexpr uint32_t ARM64_RELOC_TLVP_LOAD_PAGE21    = 8;
inline constexpr uint32_t ARM64_RELOC_TLVP_LOAD_PAGEOFF12 = 9;
inline constexpr uint32_t ARM64_RELOC_ADDEND              = 10;

// X86_64 Mach-O Relocations
inline constexpr uint32_t X86_64_RELOC_UNSIGNED           = 0;
inline constexpr uint32_t X86_64_RELOC_SIGNED             = 1;
inline constexpr uint32_t X86_64_RELOC_BRANCH             = 2;
inline constexpr uint32_t X86_64_RELOC_GOT_LOAD           = 3;
inline constexpr uint32_t X86_64_RELOC_GOT                = 4;
inline constexpr uint32_t X86_64_RELOC_SUBTRACTOR         = 5;
inline constexpr uint32_t X86_64_RELOC_SIGNED_1           = 6;
inline constexpr uint32_t X86_64_RELOC_SIGNED_2           = 7;
inline constexpr uint32_t X86_64_RELOC_SIGNED_4           = 8;
inline constexpr uint32_t X86_64_RELOC_TLV                = 9;

} // namespace dgtl::linker::macho

export namespace dgtl::backend::linker::macho {
    using namespace dgtl::linker::macho;
} // namespace dgtl::backend::linker::macho



