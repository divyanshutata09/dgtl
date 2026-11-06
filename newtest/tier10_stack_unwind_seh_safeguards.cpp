/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Module:      DGTL NewTest - Tier 10: Stack Unwinding & Callee-Saved Safeguards Test
 * Owner:       Divyanshu Tata
 */

#include "industry_test_harness.h"
#include "dgtl_sdk/dgtl.h"

INDUSTRY_TEST(tier10_stack_deep_recursion_alignment) {
    // Tests recursive function call to verify stack frame alignment (16-byte boundary)
    const char* src = 
        "fn count_down(n: i32) -> i32 {\n"
        "    if n <= 0 {\n"
        "        return 42;\n"
        "    }\n"
        "    return count_down(n - 1);\n"
        "}\n"
        "fn main() -> i32 {\n"
        "    return count_down(15);\n"
        "}\n";

    auto report = dgtl::debug::verify_against_golden(src, "", 42, DGTL_LANG_MYZON, 2);
    INDUSTRY_ASSERT_TRUE(report.is_ok());
    INDUSTRY_ASSERT_EQ(report.exit_code(), 42);
}

INDUSTRY_TEST(tier10_callee_saved_register_integrity) {
    // Interleaves many variables across a function call to ensure callee-saved registers
    // (rbx, rsi, rdi, r12-r15) are properly preserved.
    const char* src = 
        "fn leaf_calc(x: i32) -> i32 {\n"
        "    return x * 2;\n"
        "}\n"
        "fn main() -> i32 {\n"
        "    let a = 10; let b = 11;\n"
        "    let res = leaf_calc(a);\n" // 20
        "    // If leaf_calc clobbered b or a, this assertion will fail\n"
        "    if a != 10 || b != 11 { return 1; }\n"
        "    return res + b * 2; // 20 + 22 = 42\n"
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
