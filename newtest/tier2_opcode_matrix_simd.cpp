/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Module:      DGTL NewTest - Tier 2: SIMD Vector Primitives & Vectorization Test
 * Owner:       Divyanshu Tata
 */

#include "industry_test_harness.h"
#include "dgtl_sdk/dgtl.h"

INDUSTRY_TEST(tier2_simd_vector_loop_auto_vectorization) {
    const char* src = 
        "fn main() -> i32 {\n"
        "    let mut vec_a = [10, 10, 10, 10];\n"
        "    let mut vec_b = [0, 1, 2, 9];\n" // sum is 12
        "    let mut sum = 0;\n"
        "    for i in 0..4 {\n"
        "        sum = sum + vec_a[i] + vec_b[i];\n" // 40 + 12 = 52 - 10 = 42
        "    }\n"
        "    return sum - 10;\n" // 42
        "}\n";

    dgtl_debug_options_t opts;
    dgtl_debug_default_options(&opts, DGTL_LANG_MYZON);
    opts.opt_level = 3; // Enables SIMD auto-vectorizer

    auto report = dgtl::debug::verify_against_golden(src, "", 42, DGTL_LANG_MYZON, 3);
    INDUSTRY_ASSERT_TRUE(report.is_ok());
    INDUSTRY_ASSERT_EQ(report.exit_code(), 42);
}

INDUSTRY_TEST(tier2_simd_dot_product_reduction) {
    const char* src = 
        "fn main() -> i32 {\n"
        "    let x = [1, 2, 3, 4];\n"
        "    let y = [4, 3, 2, 1];\n"
        "    let mut dot = 0;\n"
        "    for i in 0..4 {\n"
        "        dot = dot + (x[i] * y[i]);\n" // 4 + 6 + 6 + 4 = 20
        "    }\n"
        "    return dot + 22;\n" // 42
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
