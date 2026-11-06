/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Module:      DGTL NewTest - Tier 11: Production Forensic Debugger Gate (dgtl-debugger)
 * Owner:       Divyanshu Tata
 */

#include "industry_test_harness.h"
#include "dgtl_sdk/dgtl.h"

INDUSTRY_TEST(tier11_toolchain_debugger_pe_audit_verification) {
    // Tests debugger PE integrity and stack unwinding invariants
    const char* src = 
        "fn recursive_fact(n: i64) -> i64 {\n"
        "    if n <= 1 {\n"
        "        return 1;\n"
        "    }\n"
        "    return n * recursive_fact(n - 1);\n"
        "}\n"
        "fn main() -> i64 {\n"
        "    return recursive_fact(5);\n" // 5! = 120
        "}\n";

    dgtl_debug_options_t opts;
    dgtl_debug_default_options(&opts, DGTL_LANG_MYZON);
    opts.run_sovereign_link = true;
    opts.opt_level = 2;

    auto report = dgtl_debug_verify_entire_stack(src, &opts);
    INDUSTRY_ASSERT_EQ(report.layer_11_sovereign_linker.status, DGTL_LAYER_PASS);
    INDUSTRY_ASSERT_EQ(report.layer_12_jit_execution.status, DGTL_LAYER_PASS);
    INDUSTRY_ASSERT_EQ(report.dgtl_exit_code, 120);

    dgtl_debug_free_report(&report);
}

#ifndef DGTL_COMBINED_SUITE
int main() {
    return dgtl::test::IndustryTestRunner::instance().run_all();
}
#endif
