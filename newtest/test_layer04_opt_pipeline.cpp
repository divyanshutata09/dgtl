/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Module:      DGTL NewTest - Layer 4: 26 Optimization Passes Pipeline Test
 * Owner:       Divyanshu Tata
 */

#include "industry_test_harness.h"
#include "dgtl_sdk/dgtl.h"

INDUSTRY_TEST(layer04_opt_levels_progression) {
    const char* src = 
        "fn main() -> i32 {\n"
        "    let a = 100;\n"
        "    let b = 200;\n"
        "    let c = a + b;\n"
        "    let unused = 999;\n"
        "    return c;\n"
        "}\n";

    for (uint32_t opt = 0; opt <= 3; ++opt) {
        dgtl_debug_options_t opts;
        dgtl_debug_default_options(&opts, DGTL_LANG_MYZON);
        opts.opt_level = opt;

        auto report = dgtl_debug_verify_entire_stack(src, &opts);
        INDUSTRY_ASSERT_EQ(report.layer_4_ir_passes.status, DGTL_LAYER_PASS);
        INDUSTRY_ASSERT_TRUE(report.ir_node_count > 0);
        dgtl_debug_free_report(&report);
    }
}

INDUSTRY_TEST(layer04_pass_bisection_api) {
    const char* src = "fn compute() -> i32 { return 42; }\n";
    dgtl_debug_options_t opts;
    dgtl_debug_default_options(&opts, DGTL_LANG_MYZON);

    const char* bisect_result = dgtl_debug_bisect_failing_pass(src, &opts);
    INDUSTRY_ASSERT_NOT_NULL(bisect_result);
    INDUSTRY_ASSERT_TRUE(std::string(bisect_result).find("Complete") != std::string::npos);
}

#ifndef DGTL_COMBINED_SUITE
int main() {
    return dgtl::test::IndustryTestRunner::instance().run_all();
}
#endif
