/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Module:      DGTL NewTest - Tier 3: LICM & Inlining Optimization Passes Test
 * Owner:       Divyanshu Tata
 */

#include "industry_test_harness.h"
#include "dgtl_sdk/dgtl.h"

INDUSTRY_TEST(tier3_pass_licm_loop_hoisting) {
    const char* src = 
        "fn main() -> i32 {\n"
        "    let invariant_factor = 2 * 3 * 7;\n" // 42
        "    let mut sum = 0;\n"
        "    for i in 0..1 {\n"
        "        let hoisted = invariant_factor;\n"
        "        sum = sum + hoisted;\n"
        "    }\n"
        "    return sum;\n" // 42
        "}\n";

    auto report = dgtl::debug::verify_against_golden(src, "", 42, DGTL_LANG_MYZON, 2);
    INDUSTRY_ASSERT_TRUE(report.is_ok());
    INDUSTRY_ASSERT_EQ(report.exit_code(), 42);
}

INDUSTRY_TEST(tier3_pass_function_inlining) {
    const char* src = 
        "fn helper_add(a: i32, b: i32) -> i32 {\n"
        "    return a + b;\n"
        "}\n"
        "fn main() -> i32 {\n"
        "    let r = helper_add(40, 2);\n"
        "    return r;\n"
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
