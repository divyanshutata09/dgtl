/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Module:      DGTL NewTest - Polyglot Differential Oracle Test
 * Owner:       Divyanshu Tata
 */

#include "industry_test_harness.h"
#include "dgtl_sdk/dgtl.h"

INDUSTRY_TEST(oracle_myzon_golden_match) {
    const char* myzon = 
        "fn main() -> i32 {\n"
        "    return 42;\n"
        "}\n";

    auto report = dgtl::debug::verify_against_golden(myzon, "", 42, DGTL_LANG_MYZON, 2);
    INDUSTRY_ASSERT_TRUE(report.is_ok());
    INDUSTRY_ASSERT_EQ(report.exit_code(), 42);
    INDUSTRY_ASSERT_EQ(report->verdict, DGTL_DEBUG_OK);
}

INDUSTRY_TEST(oracle_c_golden_match) {
    const char* c_code = 
        "int main() {\n"
        "    return 42;\n"
        "}\n";

    auto report = dgtl::debug::verify_against_golden(c_code, "", 42, DGTL_LANG_C, 2);
    INDUSTRY_ASSERT_TRUE(report.is_ok());
    INDUSTRY_ASSERT_EQ(report.exit_code(), 42);
}

INDUSTRY_TEST(oracle_rust_golden_match) {
    const char* rust_code = 
        "fn main() {\n"
        "    std::process::exit(42);\n"
        "}\n";

    auto report = dgtl::debug::verify_against_golden(rust_code, "", 42, DGTL_LANG_RUST, 2);
    INDUSTRY_ASSERT_TRUE(report.is_ok());
    INDUSTRY_ASSERT_EQ(report.exit_code(), 42);
}

INDUSTRY_TEST(oracle_python_golden_match) {
    const char* py_code = 
        "import sys\n"
        "sys.exit(42)\n";

    auto report = dgtl::debug::verify_against_golden(py_code, "", 42, DGTL_LANG_PYTHON, 2);
    INDUSTRY_ASSERT_TRUE(report.is_ok());
    INDUSTRY_ASSERT_EQ(report.exit_code(), 42);
}

INDUSTRY_TEST(oracle_intentional_deviation_detection) {
    const char* code = "fn main() -> i32 { return 42; }\n";

    // Intentionally pass an incorrect expected exit code (99 != 42)
    // The oracle MUST catch this and report that DGTL / Golden did not match!
    auto report = dgtl::debug::verify_against_golden(code, "", 99, DGTL_LANG_MYZON, 2);

    INDUSTRY_ASSERT_TRUE(!report.is_ok());
    INDUSTRY_ASSERT_EQ(report->verdict, DGTL_DEBUG_MISCOMPILATION);
    INDUSTRY_ASSERT_NOT_NULL(report->root_cause_explanation);
}

#ifndef DGTL_COMBINED_SUITE
int main() {
    return dgtl::test::IndustryTestRunner::instance().run_all();
}
#endif
