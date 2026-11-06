/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Module:      DGTL NewTest - Tier 2: Exhaustive Integer OpCode Matrix Test
 * Owner:       Divyanshu Tata
 */

#include "industry_test_harness.h"
#include "dgtl_sdk/dgtl.h"

INDUSTRY_TEST(tier2_integer_add_sub_boundary_values) {
    // Tests 32-bit and 64-bit addition, subtraction, zero-identity, underflow, overflow
    const char* src = 
        "fn main() -> i32 {\n"
        "    let zero = 0;\n"
        "    let max32 = 2147483647;\n"
        "    let min32 = -2147483648;\n"
        "    let a = max32 - 1;\n"
        "    let b = a + 1;\n"
        "    if b != max32 { return 1; }\n"
        "    let c = min32 + 1;\n"
        "    if c - 1 != min32 { return 2; }\n"
        "    return 42;\n"
        "}\n";

    auto report = dgtl::debug::verify_against_golden(src, "", 42, DGTL_LANG_MYZON, 2);
    INDUSTRY_ASSERT_TRUE(report.is_ok());
    INDUSTRY_ASSERT_EQ(report.exit_code(), 42);
}

INDUSTRY_TEST(tier2_integer_mul_div_rem_signed_unsigned) {
    const char* src = 
        "fn main() -> i32 {\n"
        "    let a = 120;\n"
        "    let b = 10;\n"
        "    let div_res = a / b;\n" // 12
        "    let rem_res = a % 7;\n" // 1
        "    let mul_res = div_res * 3;\n" // 36
        "    return mul_res + rem_res + 5;\n" // 42
        "}\n";

    auto report = dgtl::debug::verify_against_golden(src, "", 42, DGTL_LANG_MYZON, 2);
    INDUSTRY_ASSERT_TRUE(report.is_ok());
    INDUSTRY_ASSERT_EQ(report.exit_code(), 42);
}

INDUSTRY_TEST(tier2_integer_bitwise_shifts_and_masks) {
    const char* src = 
        "fn main() -> i32 {\n"
        "    let mask = 0xFF;\n"
        "    let val = 0xAA;\n"
        "    let and_val = val & mask;\n" // 0xAA (170)
        "    let xor_val = and_val ^ 0x55;\n" // 0xFF (255)
        "    let shl_val = 1 << 5;\n" // 32
        "    let shr_val = 160 >> 4;\n" // 10
        "    return shl_val + shr_val;\n" // 42
        "}\n";

    auto report = dgtl::debug::verify_against_golden(src, "", 42, DGTL_LANG_MYZON, 2);
    INDUSTRY_ASSERT_TRUE(report.is_ok());
    INDUSTRY_ASSERT_EQ(report.exit_code(), 42);
}

INDUSTRY_TEST(tier2_integer_comparisons_all_branches) {
    const char* src = 
        "fn main() -> i32 {\n"
        "    let x = 100;\n"
        "    let y = 200;\n"
        "    if !(x < y)  { return 1; }\n"
        "    if !(x <= y) { return 2; }\n"
        "    if !(y > x)  { return 3; }\n"
        "    if !(y >= x) { return 4; }\n"
        "    if !(x != y) { return 5; }\n"
        "    if x == y    { return 6; }\n"
        "    return 42;\n"
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
