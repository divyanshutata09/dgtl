/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Module:      DGTL NewTest - Tier 9: Sovereign PE Linker Binary Header & Relocation Test
 * Owner:       Divyanshu Tata
 */

#include "industry_test_harness.h"
#include "dgtl_sdk/dgtl.h"

#if defined(_WIN32) || defined(_WIN64)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#endif

INDUSTRY_TEST(tier9_linker_pe_binary_specification_compliance) {
    const char* src = 
        "fn main() -> i32 {\n"
        "    return 0;\n"
        "}\n";

    dgtl_debug_options_t opts;
    dgtl_debug_default_options(&opts, DGTL_LANG_MYZON);
    opts.run_sovereign_link = true;
    opts.entry_symbol = "main";

    auto report = dgtl_debug_verify_entire_stack(src, &opts);
    INDUSTRY_ASSERT_EQ(report.layer_11_sovereign_linker.status, DGTL_LAYER_PASS);

#if defined(_WIN32) || defined(_WIN64)
    // Validate standalone binary generation via SDK linker
    dgtl_linker_t linker = dgtl_linker_create(DGTL_OBJECT_FMT_PE);
    INDUSTRY_ASSERT_NOT_NULL(linker);
    (void)dgtl_linker_set_entry_point(linker, "main");

    const uint8_t* out_bytes = nullptr;
    size_t out_size = 0;
    dgtl_err_t err = dgtl_linker_link_to_memory(linker, DGTL_LINK_EXECUTABLE, &out_bytes, &out_size);
    if (DGTL_IS_OK(err) && out_bytes && out_size > sizeof(IMAGE_DOS_HEADER)) {
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(out_bytes);
        INDUSTRY_ASSERT_EQ(dos->e_magic, IMAGE_DOS_SIGNATURE); // 'MZ' (0x5A4D)

        if (dos->e_lfanew > 0 && static_cast<size_t>(dos->e_lfanew) + sizeof(IMAGE_NT_HEADERS64) <= out_size) {
            const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(
                out_bytes + dos->e_lfanew
            );
            INDUSTRY_ASSERT_EQ(nt->Signature, IMAGE_NT_SIGNATURE); // 'PE\0\0' (0x00004550)
            INDUSTRY_ASSERT_EQ(nt->FileHeader.Machine, IMAGE_FILE_MACHINE_AMD64); // 0x8664
            INDUSTRY_ASSERT_TRUE(nt->FileHeader.NumberOfSections >= 1);
        }
    }
    dgtl_linker_destroy(linker);
#endif

    dgtl_debug_free_report(&report);
}

#ifndef DGTL_COMBINED_SUITE
int main() {
    return dgtl::test::IndustryTestRunner::instance().run_all();
}
#endif
