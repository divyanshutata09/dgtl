/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Module:      DGTL NewTest - Layer 12: In-Memory JIT Execution Engine Test
 * Owner:       Divyanshu Tata
 */

#include "industry_test_harness.h"
#include "dgtl_sdk/dgtl.h"

INDUSTRY_TEST(layer12_jit_execution_exit_code) {
    const char* src = 
        "fn main() -> i32 {\n"
        "    let x = 40;\n"
        "    let y = 2;\n"
        "    return x + y;\n"
        "}\n";

    dgtl_debug_options_t opts;
    dgtl_debug_default_options(&opts, DGTL_LANG_MYZON);
    opts.opt_level = 2;

    auto report = dgtl_debug_verify_entire_stack(src, &opts);
    INDUSTRY_ASSERT_EQ(report.layer_12_jit_execution.status, DGTL_LAYER_PASS);
    INDUSTRY_ASSERT_EQ(report.dgtl_exit_code, 42);
    INDUSTRY_ASSERT_TRUE(report.dgtl_is_ok);
    INDUSTRY_ASSERT_EQ(report.verdict, DGTL_DEBUG_OK);
    dgtl_debug_free_report(&report);
}

INDUSTRY_TEST(layer12_jit_execution_zero_return) {
    const char* src = 
        "fn main() -> i32 {\n"
        "    return 0;\n"
        "}\n";

    dgtl_debug_options_t opts;
    dgtl_debug_default_options(&opts, DGTL_LANG_MYZON);

    auto report = dgtl_debug_verify_entire_stack(src, &opts);
    INDUSTRY_ASSERT_EQ(report.layer_12_jit_execution.status, DGTL_LAYER_PASS);
    INDUSTRY_ASSERT_TRUE(report.dgtl_is_ok);
    dgtl_debug_free_report(&report);
}

#ifndef DGTL_COMBINED_SUITE
int main() {
    return dgtl::test::IndustryTestRunner::instance().run_all();
}
#endif
