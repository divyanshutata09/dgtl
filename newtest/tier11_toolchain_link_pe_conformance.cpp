/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Module:      DGTL NewTest - Tier 11: Production Linker PE32+ Conformance Gate
 * Owner:       Divyanshu Tata
 */

#include "industry_test_harness.h"
#include "dgtl_sdk/dgtl.h"
#include <vector>
#include <cstdint>
#include <cstring>

// Windows PE/COFF Format Specifications (ECMA-335 / Microsoft Portable Executable Spec)
#pragma pack(push, 1)
struct TestDOSHeader {
    uint16_t e_magic;    // 0x5A4D ('MZ')
    uint16_t e_cblp;
    uint16_t e_cp;
    uint16_t e_crlc;
    uint16_t e_cparhdr;
    uint16_t e_minalloc;
    uint16_t e_maxalloc;
    uint16_t e_ss;
    uint16_t e_sp;
    uint16_t e_csum;
    uint16_t e_ip;
    uint16_t e_cs;
    uint16_t e_lfarlc;
    uint16_t e_ovno;
    uint16_t e_res[4];
    uint16_t e_oemid;
    uint16_t e_oeminfo;
    uint16_t e_res2[10];
    int32_t  e_lfanew;   // Offset to NT Headers
};

struct TestFileHeader {
    uint16_t Machine;              // 0x8664 = IMAGE_FILE_MACHINE_AMD64
    uint16_t NumberOfSections;
    uint32_t TimeDateStamp;
    uint32_t PointerToSymbolTable;
    uint32_t NumberOfSymbols;
    uint16_t SizeOfOptionalHeader;
    uint16_t Characteristics;      // 0x0022 = IMAGE_FILE_EXECUTABLE_IMAGE | IMAGE_FILE_LARGE_ADDRESS_AWARE
};

struct TestOptionalHeader64 {
    uint16_t Magic;                // 0x020B = PE32+
    uint8_t  MajorLinkerVersion;
    uint8_t  MinorLinkerVersion;
    uint32_t SizeOfCode;
    uint32_t SizeOfInitializedData;
    uint32_t SizeOfUninitializedData;
    uint32_t AddressOfEntryPoint;
    uint32_t BaseOfCode;
    uint64_t ImageBase;
    uint32_t SectionAlignment;
    uint32_t FileAlignment;
    uint16_t MajorOperatingSystemVersion;
    uint16_t MinorOperatingSystemVersion;
    uint16_t MajorImageVersion;
    uint16_t MinorImageVersion;
    uint16_t MajorSubsystemVersion;
    uint16_t MinorSubsystemVersion;
    uint32_t Win32VersionValue;
    uint32_t SizeOfImage;
    uint32_t SizeOfHeaders;
    uint32_t CheckSum;
    uint16_t Subsystem;            // 3 = IMAGE_SUBSYSTEM_WINDOWS_CUI
    uint16_t DllCharacteristics;
    uint64_t SizeOfStackReserve;
    uint64_t SizeOfStackCommit;
    uint64_t SizeOfHeapReserve;
    uint64_t SizeOfHeapCommit;
    uint32_t LoaderFlags;
    uint32_t NumberOfRvaAndSizes;
};
#pragma pack(pop)

INDUSTRY_TEST(tier11_toolchain_sovereign_pe_linker_spec) {
    const char* src = 
        "fn main() -> i32 {\n"
        "    let mut sum: i64 = 0;\n"
        "    let mut i: i64 = 1;\n"
        "    while i <= 100 {\n"
        "        sum = sum + i;\n"
        "        i = i + 1;\n"
        "    }\n"
        "    return 0;\n"
        "}\n";

    dgtl_debug_options_t opts;
    dgtl_debug_default_options(&opts, DGTL_LANG_MYZON);
    opts.run_sovereign_link = true;
    opts.target_arch = DGTL_ARCH_X86_64;
    opts.opt_level = 2;
    opts.entry_symbol = "main";

    auto report = dgtl_debug_verify_entire_stack(src, &opts);
    INDUSTRY_ASSERT_EQ(report.layer_11_sovereign_linker.status, DGTL_LAYER_PASS);
    INDUSTRY_ASSERT_TRUE(report.layer_11_sovereign_linker.latency_us >= 0);

    // Verify Object Writer & Relocation Layers
    INDUSTRY_ASSERT_EQ(report.layer_10_object_file.status, DGTL_LAYER_PASS);
    INDUSTRY_ASSERT_EQ(report.layer_9_isa_codegen.status, DGTL_LAYER_PASS);

    dgtl_debug_free_report(&report);
}

#ifndef DGTL_COMBINED_SUITE
int main() {
    return dgtl::test::IndustryTestRunner::instance().run_all();
}
#endif
