/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Module:      DGTL NewTest - Tier 11: Production Assembler & MC Encoder Spec
 * Owner:       Divyanshu Tata
 */

#include "industry_test_harness.h"
#include "dgtl_sdk/dgtl.h"

INDUSTRY_TEST(tier11_toolchain_as_assembler_loop_encoding) {
    // Tests assembler branch displacement encodings and loop labels:
    // Computes sum of squares of integers from 1 to 10:
    // sum = 1^2 + 2^2 + ... + 10^2 = 10*(11)*(21)/6 = 385
    const char* src = 
        "fn main() -> i64 {\n"
        "    let mut sum: i64 = 0;\n"
        "    let mut n: i64 = 1;\n"
        "    while n <= 10 {\n"
        "        let sq: i64 = n * n;\n"
        "        sum = sum + sq;\n"
        "        n = n + 1;\n"
        "    }\n"
        "    return sum;\n"
        "}\n";

    dgtl_debug_options_t opts;
    dgtl_debug_default_options(&opts, DGTL_LANG_MYZON);
    opts.target_arch = DGTL_ARCH_X86_64;
    opts.opt_level = 1;

    auto report = dgtl_debug_verify_entire_stack(src, &opts);
    INDUSTRY_ASSERT_EQ(report.layer_6_mir_isel.status, DGTL_LAYER_PASS);
    INDUSTRY_ASSERT_EQ(report.layer_7_regalloc.status, DGTL_LAYER_PASS);
    INDUSTRY_ASSERT_EQ(report.layer_8_mir_pipeline.status, DGTL_LAYER_PASS);
    INDUSTRY_ASSERT_TRUE(report.dgtl_is_ok);
    INDUSTRY_ASSERT_EQ(report.dgtl_exit_code, 42);

    dgtl_debug_free_report(&report);
}

#ifndef DGTL_COMBINED_SUITE
int main() {
    return dgtl::test::IndustryTestRunner::instance().run_all();
}
#endif
