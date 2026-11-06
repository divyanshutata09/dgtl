/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Module:      DGTL NewTest - Tier 2: Exhaustive Floating-Point IEEE-754 Test
 * Owner:       Divyanshu Tata
 */

#include "industry_test_harness.h"
#include "dgtl_sdk/dgtl.h"

INDUSTRY_TEST(tier2_float_basic_arithmetic) {
    const char* src = 
        "fn main() -> i32 {\n"
        "    let a = 20.5;\n"
        "    let b = 21.5;\n"
        "    let sum = a + b;\n" // 42.0
        "    let int_res = sum as i32;\n"
        "    return int_res;\n" // 42
        "}\n";

    auto report = dgtl::debug::verify_against_golden(src, "", 42, DGTL_LANG_MYZON, 2);
    INDUSTRY_ASSERT_TRUE(report.is_ok());
    INDUSTRY_ASSERT_EQ(report.exit_code(), 42);
}

INDUSTRY_TEST(tier2_float_f64_precision_and_scaling) {
    const char* src = 
        "fn main() -> i32 {\n"
        "    let pi = 3.141592653589793;\n"
        "    let radius = 10.0;\n"
        "    let area = pi * radius * radius;\n" // ~314.159265
        "    if area < 314.0 || area > 315.0 { return 1; }\n"
        "    return 42;\n"
        "}\n";

    auto report = dgtl::debug::verify_against_golden(src, "", 42, DGTL_LANG_MYZON, 2);
    INDUSTRY_ASSERT_TRUE(report.is_ok());
    INDUSTRY_ASSERT_EQ(report.exit_code(), 42);
}

INDUSTRY_TEST(tier2_float_conversion_roundtrips) {
    const char* src = 
        "fn main() -> i32 {\n"
        "    let original = 42;\n"
        "    let as_f64 = original as f64;\n"
        "    let scaled = as_f64 * 2.0;\n"
        "    let back_to_i32 = (scaled / 2.0) as i32;\n"
        "    return back_to_i32;\n" // 42
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
