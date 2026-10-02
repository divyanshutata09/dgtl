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

export module dgtl.linker.pe.relocations;

import dgtl.object.relocation;

export namespace dgtl::linker::pe {

inline constexpr uint16_t IMAGE_REL_AMD64_ABSOLUTE = 0x0000;
inline constexpr uint16_t IMAGE_REL_AMD64_ADDR64   = 0x0001;
inline constexpr uint16_t IMAGE_REL_AMD64_ADDR32   = 0x0002;
inline constexpr uint16_t IMAGE_REL_AMD64_REL32    = 0x0004;

} // namespace dgtl::linker::pe

export namespace dgtl::backend::linker::pe {
    using namespace dgtl::linker::pe;
} // namespace dgtl::backend::linker::pe



