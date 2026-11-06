/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Module:      DGTL NewTest - Tier 4: Deep CFG Control-Flow Stress Test
 * Owner:       Divyanshu Tata
 */

#include "industry_test_harness.h"
#include "dgtl_sdk/dgtl.h"

INDUSTRY_TEST(tier4_stress_deep_chained_branches) {
    // Generate a deep CFG with 30 chained if-else blocks
    std::string src = 
        "fn main() -> i32 {\n"
        "    let mut acc = 0;\n";
    for (int i = 0; i < 30; ++i) {
        src += "    if acc >= 0 {\n";
        src += "        acc = acc + 1;\n";
        src += "    } else {\n";
        src += "        acc = acc - 1;\n";
        src += "    }\n";
    }
    src += "    // acc is 30\n";
    src += "    return acc + 12;\n"; // 42
    src += "}\n";

    dgtl_debug_options_t opts;
    dgtl_debug_default_options(&opts, DGTL_LANG_MYZON);
    opts.opt_level = 2;

    auto report = dgtl_debug_verify_entire_stack(src.c_str(), &opts);
    INDUSTRY_ASSERT_EQ(report.layer_6_mir_isel.status, DGTL_LAYER_PASS);
    INDUSTRY_ASSERT_EQ(report.layer_8_mir_pipeline.status, DGTL_LAYER_PASS);
    INDUSTRY_ASSERT_EQ(report.dgtl_exit_code, 42);
    INDUSTRY_ASSERT_TRUE(report.dgtl_is_ok);
    dgtl_debug_free_report(&report);
}

#ifndef DGTL_COMBINED_SUITE
int main() {
    return dgtl::test::IndustryTestRunner::instance().run_all();
}
#endif
