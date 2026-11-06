/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Module:      DGTL NewTest - Layer 6: MIR Instruction Selection (ISel) Test
 * Owner:       Divyanshu Tata
 */

#include "industry_test_harness.h"
#include "dgtl_sdk/dgtl.h"

INDUSTRY_TEST(layer06_isel_arithmetic_trees) {
    const char* src = 
        "fn main() -> i32 {\n"
        "    let a = 12;\n"
        "    let b = 34;\n"
        "    let c = 56;\n"
        "    return (a * b) + (c - a);\n"
        "}\n";

    dgtl_debug_options_t opts;
    dgtl_debug_default_options(&opts, DGTL_LANG_MYZON);

    auto report = dgtl_debug_verify_entire_stack(src, &opts);
    INDUSTRY_ASSERT_EQ(report.layer_6_mir_isel.status, DGTL_LAYER_PASS);
    INDUSTRY_ASSERT_TRUE(report.layer_6_mir_isel.latency_us >= 0);
    dgtl_debug_free_report(&report);
}

#ifndef DGTL_COMBINED_SUITE
int main() {
    return dgtl::test::IndustryTestRunner::instance().run_all();
}
#endif
