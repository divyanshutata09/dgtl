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

export module dgtl.linker.pe.sections;

export namespace dgtl::linker::pe {

struct SectionDescriptor {
    const char* name;
    uint64_t alignment;
    uint32_t flags;
};

inline constexpr uint32_t IMAGE_SCN_CNT_CODE               = 0x00000020;
inline constexpr uint32_t IMAGE_SCN_CNT_INITIALIZED_DATA   = 0x00000040;
inline constexpr uint32_t IMAGE_SCN_CNT_UNINITIALIZED_DATA = 0x00000080;
inline constexpr uint32_t IMAGE_SCN_MEM_EXECUTE            = 0x20000000;
inline constexpr uint32_t IMAGE_SCN_MEM_READ               = 0x40000000;
inline constexpr uint32_t IMAGE_SCN_MEM_WRITE              = 0x80000000;

} // namespace dgtl::linker::pe

export namespace dgtl::backend::linker::pe {
    using namespace dgtl::linker::pe;
} // namespace dgtl::backend::linker::pe



