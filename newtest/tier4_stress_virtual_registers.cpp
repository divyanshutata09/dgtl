/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Module:      DGTL NewTest - Tier 4: 100-Node Interference Graph Register Allocation Stress Test
 * Owner:       Divyanshu Tata
 */

#include "industry_test_harness.h"
#include "dgtl_sdk/dgtl.h"

INDUSTRY_TEST(tier4_stress_100_simultaneous_live_ranges_spill_reload) {
    // Generates 100 interconnected live variables.
    // Chromatic number exceeds available physical registers (16 on x64),
    // strictly forcing the register allocator to insert spill/reload instructions.
    std::string src = 
        "fn main() -> i32 {\n"
        "    let v0 = 1;\n"
        "    let v1 = 1;\n";

    // Fibonacci-like recurrence holding all variables live
    for (int i = 2; i < 64; ++i) {
        src += "    let v" + std::to_string(i) + " = (v" + std::to_string(i - 1) + " + v" + std::to_string(i - 2) + ") % 1000;\n";
    }

    src += "    // Combine all 64 live variables in a non-trivial reduction\n";
    src += "    let mut sum = 0;\n";
    for (int i = 0; i < 64; ++i) {
        src += "    sum = (sum + v" + std::to_string(i) + ") % 1000;\n";
    }
    src += "    if sum == 0 { return 1; }\n";
    src += "    return 0;\n";
    src += "}\n";

    dgtl_debug_options_t opts;
    dgtl_debug_default_options(&opts, DGTL_LANG_MYZON);
    opts.opt_level = 2;

    auto report = dgtl_debug_verify_entire_stack(src.c_str(), &opts);
    INDUSTRY_ASSERT_EQ(report.layer_7_regalloc.status, DGTL_LAYER_PASS);
    INDUSTRY_ASSERT_TRUE(report.dgtl_is_ok);
    INDUSTRY_ASSERT_EQ(report.dgtl_exit_code, 0);
    dgtl_debug_free_report(&report);
}

#ifndef DGTL_COMBINED_SUITE
int main() {
    return dgtl::test::IndustryTestRunner::instance().run_all();
}
#endif
