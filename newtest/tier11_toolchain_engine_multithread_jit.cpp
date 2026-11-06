/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Module:      DGTL NewTest - Tier 11: Production JIT Engine Multi-Iteration Gate
 * Owner:       Divyanshu Tata
 */

#include "industry_test_harness.h"
#include "dgtl_sdk/dgtl.h"

INDUSTRY_TEST(tier11_toolchain_engine_collatz_convergence) {
    // Tests Collatz 3n+1 trajectory convergence in JIT:
    // Starting with n = 27 (a notoriously long hailstone sequence of 111 steps):
    // Computes the exact number of steps until n reaches 1.
    const char* src = 
        "fn main() -> i32 {\n"
        "    let mut n: i64 = 27;\n"
        "    let mut steps: i32 = 0;\n"
        "    while n > 1 {\n"
        "        if n % 2 == 0 {\n"
        "            n = n / 2;\n"
        "        } else {\n"
        "            n = 3 * n + 1;\n"
        "        }\n"
        "        steps = steps + 1;\n"
        "    }\n"
        "    return steps;\n" // Proven mathematical constant: 111 steps
        "}\n";

    dgtl_debug_options_t opts;
    dgtl_debug_default_options(&opts, DGTL_LANG_MYZON);
    opts.opt_level = 2;

    auto report = dgtl_debug_verify_entire_stack(src, &opts);
    INDUSTRY_ASSERT_EQ(report.layer_12_jit_execution.status, DGTL_LAYER_PASS);
    INDUSTRY_ASSERT_EQ(report.dgtl_exit_code, 111);

    dgtl_debug_free_report(&report);
}

INDUSTRY_TEST(tier11_toolchain_engine_horner_polynomial) {
    // Evaluates 5th-degree polynomial using Horner's Method:
    // P(x) = 3x^5 - 2x^4 + 7x^3 - 5x^2 + 11x - 17 at x = 3
    // P(3) = 3*(243) - 2*(81) + 7*(27) - 5*(9) + 11*(3) - 17
    //      = 729 - 162 + 189 - 45 + 33 - 17 = 727
    const char* src = 
        "fn main() -> i64 {\n"
        "    let x: i64 = 3;\n"
        "    let mut p: i64 = 3;\n"
        "    p = p * x - 2;\n"
        "    p = p * x + 7;\n"
        "    p = p * x - 5;\n"
        "    p = p * x + 11;\n"
        "    p = p * x - 17;\n"
        "    return p;\n" // Must equal exactly 727
        "}\n";

    dgtl_debug_options_t opts;
    dgtl_debug_default_options(&opts, DGTL_LANG_MYZON);
    opts.opt_level = 3;

    auto report = dgtl_debug_verify_entire_stack(src, &opts);
    INDUSTRY_ASSERT_EQ(report.layer_12_jit_execution.status, DGTL_LAYER_PASS);
    INDUSTRY_ASSERT_EQ(report.dgtl_exit_code, 727);

    dgtl_debug_free_report(&report);
}

#ifndef DGTL_COMBINED_SUITE
int main() {
    return dgtl::test::IndustryTestRunner::instance().run_all();
}
#endif
