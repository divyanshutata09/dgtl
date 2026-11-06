/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Module:      DGTL NewTest - Tier 4: Arena & Megabuffer Memory Pressure Test
 * Owner:       Divyanshu Tata
 */

#include "industry_test_harness.h"
#include "dgtl_sdk/dgtl.h"

INDUSTRY_TEST(tier4_arena_repeated_churn_isolation) {
    const char* src = "fn main() -> i32 { return 42; }\n";

    dgtl_debug_options_t opts;
    dgtl_debug_default_options(&opts, DGTL_LANG_MYZON);

    // Run 50 back-to-back full stack compiles to test memory reclamation and isolation
    for (int run = 0; run < 50; ++run) {
        auto report = dgtl_debug_verify_entire_stack(src, &opts);
        INDUSTRY_ASSERT_TRUE(report.dgtl_is_ok);
        INDUSTRY_ASSERT_EQ(report.dgtl_exit_code, 42);
        INDUSTRY_ASSERT_TRUE(report.peak_arena_bytes > 0);
        dgtl_debug_free_report(&report);
    }
}

#ifndef DGTL_COMBINED_SUITE
int main() {
    return dgtl::test::IndustryTestRunner::instance().run_all();
}
#endif
