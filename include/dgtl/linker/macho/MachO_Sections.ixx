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

export module dgtl.linker.macho.sections;

export namespace dgtl::linker::macho {

struct SectionDescriptor {
    const char* segname;
    const char* sectname;
    uint64_t alignment;
    uint32_t flags;
};

// Section types (bits 0-7)
inline constexpr uint32_t S_REGULAR                         = 0x00000000;
inline constexpr uint32_t S_ZEROFILL                        = 0x00000001;
inline constexpr uint32_t S_CSTRING_LITERALS                = 0x00000002;
inline constexpr uint32_t S_4BYTE_LITERALS                  = 0x00000003;
inline constexpr uint32_t S_8BYTE_LITERALS                  = 0x00000004;
inline constexpr uint32_t S_LITERAL_POINTERS                = 0x00000005;
inline constexpr uint32_t S_NON_LAZY_SYMBOL_POINTERS        = 0x00000006;
inline constexpr uint32_t S_LAZY_SYMBOL_POINTERS            = 0x00000007;
inline constexpr uint32_t S_SYMBOL_STUBS                    = 0x00000008;
inline constexpr uint32_t S_MOD_INIT_FUNC_POINTERS          = 0x00000009;
inline constexpr uint32_t S_MOD_TERM_FUNC_POINTERS          = 0x0000000A;
inline constexpr uint32_t S_COALESCED                       = 0x0000000B;
inline constexpr uint32_t S_GB_ZEROFILL                     = 0x0000000C;
inline constexpr uint32_t S_INTERPOSING                     = 0x0000000D;
inline constexpr uint32_t S_16BYTE_LITERALS                 = 0x0000000E;
inline constexpr uint32_t S_DTRACE_DOF                      = 0x0000000F;
inline constexpr uint32_t S_LAZY_DYLIB_SYMBOL_POINTERS      = 0x00000010;
inline constexpr uint32_t S_THREAD_LOCAL_REGULAR            = 0x00000011;
inline constexpr uint32_t S_THREAD_LOCAL_ZEROFILL           = 0x00000012;
inline constexpr uint32_t S_THREAD_LOCAL_VARIABLES          = 0x00000013;
inline constexpr uint32_t S_THREAD_LOCAL_VARIABLE_POINTERS  = 0x00000014;
inline constexpr uint32_t S_THREAD_LOCAL_INIT_FUNCTION_POINTERS = 0x00000015;

// Section attribute flags
inline constexpr uint32_t S_ATTR_PURE_INSTRUCTIONS          = 0x80000000;
inline constexpr uint32_t S_ATTR_NO_TOC                     = 0x40000000;
inline constexpr uint32_t S_ATTR_STRIP_STATIC_SYMS          = 0x20000000;
inline constexpr uint32_t S_ATTR_NO_DEAD_STRIP              = 0x10000000;
inline constexpr uint32_t S_ATTR_LIVE_SUPPORT               = 0x08000000;
inline constexpr uint32_t S_ATTR_SELF_MODIFYING_CODE        = 0x04000000;
inline constexpr uint32_t S_ATTR_DEBUG                      = 0x02000000;
inline constexpr uint32_t S_ATTR_SOME_INSTRUCTIONS          = 0x00000400;
inline constexpr uint32_t S_ATTR_EXT_RELOC                  = 0x00000200;
inline constexpr uint32_t S_ATTR_LOC_RELOC                  = 0x00000100;

// Standard Mach-O Segment Names
inline constexpr const char* SEG_TEXT                       = "__TEXT";
inline constexpr const char* SEG_DATA                       = "__DATA";
inline constexpr const char* SEG_LD                         = "__LD";
inline constexpr const char* SEG_LINKEDIT                   = "__LINKEDIT";

// Standard Mach-O Section Names
inline constexpr const char* SECT_TEXT                      = "__text";
inline constexpr const char* SECT_CONST                     = "__const";
inline constexpr const char* SECT_CSTRING                   = "__cstring";
inline constexpr const char* SECT_UNWIND_INFO               = "__unwind_info";
inline constexpr const char* SECT_EH_FRAME                  = "__eh_frame";
inline constexpr const char* SECT_DATA                      = "__data";
inline constexpr const char* SECT_BSS                       = "__bss";
inline constexpr const char* SECT_COMMON                    = "__common";
inline constexpr const char* SECT_THREAD_DATA               = "__thread_data";
inline constexpr const char* SECT_THREAD_BSS                = "__thread_bss";
inline constexpr const char* SECT_COMPACT_UNWIND            = "__compact_unwind";

} // namespace dgtl::linker::macho

export namespace dgtl::backend::linker::macho {
    using namespace dgtl::linker::macho;
} // namespace dgtl::backend::linker::macho



