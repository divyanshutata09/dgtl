/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Module:      DGTL NewTest - Layer 11: Standalone Sovereign Linkers Test
 * Owner:       Divyanshu Tata
 */

#include "industry_test_harness.h"
#include "dgtl_sdk/dgtl.h"

INDUSTRY_TEST(layer11_sovereign_pe_standalone_link) {
    const char* src = 
        "fn main() -> i32 {\n"
        "    return 0;\n"
        "}\n";

    dgtl_debug_options_t opts;
    dgtl_debug_default_options(&opts, DGTL_LANG_MYZON);
    opts.run_sovereign_link = true;
    opts.entry_symbol = "main";

    auto report = dgtl_debug_verify_entire_stack(src, &opts);
    INDUSTRY_ASSERT_EQ(report.layer_11_sovereign_linker.status, DGTL_LAYER_PASS);
    INDUSTRY_ASSERT_TRUE(report.layer_11_sovereign_linker.latency_us >= 0);
    dgtl_debug_free_report(&report);
}

#ifndef DGTL_COMBINED_SUITE
int main() {
    return dgtl::test::IndustryTestRunner::instance().run_all();
}
#endif
