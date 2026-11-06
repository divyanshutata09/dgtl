/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Module:      DGTL NewTest - Tier 3: GVN Deduplication & Dead Code Elimination Test
 * Owner:       Divyanshu Tata
 */

#include "industry_test_harness.h"
#include "dgtl_sdk/dgtl.h"

INDUSTRY_TEST(tier3_pass_gvn_common_subexpression) {
    const char* src = 
        "fn main() -> i32 {\n"
        "    let a = 20;\n"
        "    let b = 1;\n"
        "    let expr1 = a + b;\n" // 21
        "    let expr2 = a + b;\n" // 21 (identical to expr1, should be GVN folded)
        "    return expr1 + expr2;\n" // 42
        "}\n";

    dgtl_debug_options_t opt0;
    dgtl_debug_default_options(&opt0, DGTL_LANG_MYZON);
    opt0.opt_level = 0;
    auto rep0 = dgtl_debug_verify_entire_stack(src, &opt0);

    dgtl_debug_options_t opt2;
    dgtl_debug_default_options(&opt2, DGTL_LANG_MYZON);
    opt2.opt_level = 2;
    auto rep2 = dgtl_debug_verify_entire_stack(src, &opt2);

    INDUSTRY_ASSERT_EQ(rep0.dgtl_exit_code, 42);
    INDUSTRY_ASSERT_EQ(rep2.dgtl_exit_code, 42);
    INDUSTRY_ASSERT_TRUE(rep2.dgtl_is_ok);

    dgtl_debug_free_report(&rep0);
    dgtl_debug_free_report(&rep2);
}

INDUSTRY_TEST(tier3_pass_dce_pruning_dead_assignments) {
    const char* src = 
        "fn main() -> i32 {\n"
        "    let dead1 = 100 * 200;\n"
        "    let dead2 = dead1 + 500;\n"
        "    let dead3 = dead2 / 3;\n"
        "    let live = 42;\n"
        "    return live;\n"
        "}\n";

    auto report = dgtl::debug::verify_against_golden(src, "", 42, DGTL_LANG_MYZON, 2);
    INDUSTRY_ASSERT_TRUE(report.is_ok());
    INDUSTRY_ASSERT_EQ(report.exit_code(), 42);
}

#ifndef DGTL_COMBINED_SUITE
int main() {
    return dgtl::test::IndustryTestRunner::instance().run_all();
}
#endif
