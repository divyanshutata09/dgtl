/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Module:      DGTL NewTest - Layer 5: Polyhedral Loop Engine Test
 * Owner:       Divyanshu Tata
 */

#include "industry_test_harness.h"
#include "dgtl_sdk/dgtl.h"

INDUSTRY_TEST(layer05_polyhedral_affine_loops) {
    const char* src = 
        "fn matrix_sum() -> i32 {\n"
        "    let mut acc = 0;\n"
        "    for i in 0..8 {\n"
        "        for j in 0..8 {\n"
        "            acc = acc + (i * j);\n"
        "        }\n"
        "    }\n"
        "    return acc;\n"
        "}\n";

    dgtl_debug_options_t opts;
    dgtl_debug_default_options(&opts, DGTL_LANG_MYZON);
    opts.run_all_26_passes = true;
    opts.opt_level = 3;

    auto report = dgtl_debug_verify_entire_stack(src, &opts);
    INDUSTRY_ASSERT_EQ(report.layer_5_polyhedral.status, DGTL_LAYER_PASS);
    dgtl_debug_free_report(&report);
}

#ifndef DGTL_COMBINED_SUITE
int main() {
    return dgtl::test::IndustryTestRunner::instance().run_all();
}
#endif
