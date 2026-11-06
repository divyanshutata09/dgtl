/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Module:      DGTL NewTest - Layer 7: Universal Register Allocator Test
 * Owner:       Divyanshu Tata
 */

#include "industry_test_harness.h"
#include "dgtl_sdk/dgtl.h"

INDUSTRY_TEST(layer07_regalloc_pressure_handling) {
    // Generate high live-range pressure with 16 simultaneous variables
    const char* src = 
        "fn main() -> i32 {\n"
        "    let v0 = 1;  let v1 = 2;  let v2 = 3;  let v3 = 4;\n"
        "    let v4 = 5;  let v5 = 6;  let v6 = 7;  let v7 = 8;\n"
        "    let v8 = 9;  let v9 = 10; let v10 = 11; let v11 = 12;\n"
        "    let v12 = 13; let v13 = 14; let v14 = 15; let v15 = 16;\n"
        "    return v0+v1+v2+v3+v4+v5+v6+v7+v8+v9+v10+v11+v12+v13+v14+v15;\n"
        "}\n";

    dgtl_debug_options_t opts;
    dgtl_debug_default_options(&opts, DGTL_LANG_MYZON);

    auto report = dgtl_debug_verify_entire_stack(src, &opts);
    INDUSTRY_ASSERT_EQ(report.layer_7_regalloc.status, DGTL_LAYER_PASS);
    INDUSTRY_ASSERT_TRUE(report.dgtl_exit_code != 0); // Sum is 136
    dgtl_debug_free_report(&report);
}

#ifndef DGTL_COMBINED_SUITE
int main() {
    return dgtl::test::IndustryTestRunner::instance().run_all();
}
#endif
