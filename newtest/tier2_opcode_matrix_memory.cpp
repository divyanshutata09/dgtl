/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Module:      DGTL NewTest - Tier 2: Memory Load/Store & Addressing Modes Test
 * Owner:       Divyanshu Tata
 */

#include "industry_test_harness.h"
#include "dgtl_sdk/dgtl.h"

INDUSTRY_TEST(tier2_memory_array_read_write_cycles) {
    const char* src = 
        "fn main() -> i32 {\n"
        "    let mut buffer = [0, 0, 0, 0, 0, 0, 0, 0];\n"
        "    for i in 0..8 {\n"
        "        buffer[i] = i * 5;\n" // 0, 5, 10, 15, 20, 25, 30, 35
        "    }\n"
        "    let val3 = buffer[3];\n" // 15
        "    let val5 = buffer[5];\n" // 25
        "    return val3 + val5 + 2;\n" // 42
        "}\n";

    auto report = dgtl::debug::verify_against_golden(src, "", 42, DGTL_LANG_MYZON, 2);
    INDUSTRY_ASSERT_TRUE(report.is_ok());
    INDUSTRY_ASSERT_EQ(report.exit_code(), 42);
}

INDUSTRY_TEST(tier2_memory_stack_local_spilling) {
    const char* src = 
        "fn main() -> i32 {\n"
        "    let mut a = 10;\n"
        "    let mut b = 20;\n"
        "    let mut temp = a;\n"
        "    a = b;\n"
        "    b = temp;\n"
        "    if a != 20 || b != 10 { return 1; }\n"
        "    return a * 2 + 2;\n" // 42
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
