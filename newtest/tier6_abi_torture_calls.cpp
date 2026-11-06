/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Module:      DGTL NewTest - Tier 6: ABI Calling Convention Torture & Non-Trivial Parameter Passing
 * Owner:       Divyanshu Tata
 */

#include "industry_test_harness.h"
#include "dgtl_sdk/dgtl.h"

INDUSTRY_TEST(tier6_abi_12_arguments_weighted_sum_register_and_stack) {
    // 12 arguments: 4 in registers (rcx, rdx, r8, r9 on Win64), 8 on shadow stack.
    // Each parameter is weighted by its index:
    // f(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12)
    // Sum of squares from 1 to 12:
    // 1 + 4 + 9 + 16 + 25 + 36 + 49 + 64 + 81 + 100 + 121 + 144 = 650
    // If any stack displacement or register allocation is misaligned, this assertion fails immediately.
    const char* src = 
        "fn weighted_sum(a: i32, b: i32, c: i32, d: i32,\n"
        "                 e: i32, f: i32, g: i32, h: i32,\n"
        "                 i: i32, j: i32, k: i32, l: i32) -> i32 {\n"
        "    return a*1 + b*2 + c*3 + d*4 + e*5 + f*6 + g*7 + h*8 + i*9 + j*10 + k*11 + l*12;\n"
        "}\n"
        "fn main() -> i32 {\n"
        "    let res = weighted_sum(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12);\n"
        "    if res != 650 {\n"
        "        return 1;\n"
        "    }\n"
        "    return 0;\n"
        "}\n";

    auto report = dgtl::debug::verify_against_golden(src, "", 0, DGTL_LANG_MYZON, 2);
    INDUSTRY_ASSERT_TRUE(report.is_ok());
    INDUSTRY_ASSERT_EQ(report.exit_code(), 0);
}

#ifndef DGTL_COMBINED_SUITE
int main() {
    return dgtl::test::IndustryTestRunner::instance().run_all();
}
#endif
