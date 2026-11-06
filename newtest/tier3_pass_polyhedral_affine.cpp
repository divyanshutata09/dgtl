/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Module:      DGTL NewTest - Tier 3: Polyhedral Affine Loop Transformation Test
 * Owner:       Divyanshu Tata
 */

#include "industry_test_harness.h"
#include "dgtl_sdk/dgtl.h"

INDUSTRY_TEST(tier3_polyhedral_nested_tiled_loops) {
    const char* src = 
        "fn main() -> i32 {\n"
        "    let mut acc = 0;\n"
        "    for i in 0..4 {\n"
        "        for j in 0..4 {\n"
        "            acc = acc + 1;\n"
        "        }\n"
        "    }\n"
        "    // acc is 16 + 26 = 42\n"
        "    return acc + 26;\n"
        "}\n";

    dgtl_debug_options_t opts;
    dgtl_debug_default_options(&opts, DGTL_LANG_MYZON);
    opts.run_all_26_passes = true;
    opts.opt_level = 3;

    auto report = dgtl_debug_verify_entire_stack(src, &opts);
    INDUSTRY_ASSERT_EQ(report.layer_5_polyhedral.status, DGTL_LAYER_PASS);
    INDUSTRY_ASSERT_EQ(report.dgtl_exit_code, 42);
    INDUSTRY_ASSERT_TRUE(report.dgtl_is_ok);
    dgtl_debug_free_report(&report);
}

#ifndef DGTL_COMBINED_SUITE
int main() {
    return dgtl::test::IndustryTestRunner::instance().run_all();
}
#endif
