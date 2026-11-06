/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Module:      DGTL NewTest - Tier 11: Production Object File Disassembler Gate (dgtl-objdump)
 * Owner:       Divyanshu Tata
 */

#include "industry_test_harness.h"
#include "dgtl_sdk/dgtl.h"
#include <vector>
#include <cstdint>
#include <string>

INDUSTRY_TEST(tier11_toolchain_objdump_section_and_disasm) {
    // Tests object file section decoding and disassembly parsing
    const char* src = 
        "fn calculate(a: i64, b: i64) -> i64 {\n"
        "    return a * 3 + b * 7;\n"
        "}\n"
        "fn main() -> i64 {\n"
        "    return calculate(10, 20);\n"
        "}\n";

    dgtl_debug_options_t opts;
    dgtl_debug_default_options(&opts, DGTL_LANG_MYZON);
    opts.target_arch = DGTL_ARCH_X86_64;
    opts.opt_level = 2;

    auto report = dgtl_debug_verify_entire_stack(src, &opts);
    INDUSTRY_ASSERT_EQ(report.layer_10_object_file.status, DGTL_LAYER_PASS);
    INDUSTRY_ASSERT_EQ(report.layer_12_jit_execution.status, DGTL_LAYER_PASS);
    INDUSTRY_ASSERT_EQ(report.dgtl_exit_code, 170); // 10*3 + 20*7 = 30 + 140 = 170

    dgtl_debug_free_report(&report);
}

#ifndef DGTL_COMBINED_SUITE
int main() {
    return dgtl::test::IndustryTestRunner::instance().run_all();
}
#endif
