/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Module:      DGTL NewTest - Layer 2: Transactional SoA IRStream & Arena Test
 * Owner:       Divyanshu Tata
 */

#include "industry_test_harness.h"
#include "dgtl_sdk/dgtl.h"

INDUSTRY_TEST(layer02_irstream_structural_integrity) {
    const char* src = 
        "fn main() -> i32 {\n"
        "    let a = 10;\n"
        "    let b = 20;\n"
        "    let c = 30;\n"
        "    return a + b + c;\n"
        "}\n";

    dgtl_debug_options_t opts;
    dgtl_debug_default_options(&opts, DGTL_LANG_MYZON);

    auto report = dgtl_debug_verify_entire_stack(src, &opts);
    INDUSTRY_ASSERT_EQ(report.layer_2_ir_stream.status, DGTL_LAYER_PASS);
    INDUSTRY_ASSERT_TRUE(report.ir_node_count >= 4);
    INDUSTRY_ASSERT_TRUE(report.logic_hash != 0);
    INDUSTRY_ASSERT_TRUE(report.peak_arena_bytes > 0);
    dgtl_debug_free_report(&report);
}

INDUSTRY_TEST(layer02_irstream_hash_stability) {
    const char* src = "fn test() -> i32 { return 77; }\n";

    dgtl_debug_options_t opts;
    dgtl_debug_default_options(&opts, DGTL_LANG_MYZON);

    auto r1 = dgtl_debug_verify_entire_stack(src, &opts);
    auto r2 = dgtl_debug_verify_entire_stack(src, &opts);

    INDUSTRY_ASSERT_EQ(r1.layer_2_ir_stream.status, DGTL_LAYER_PASS);
    INDUSTRY_ASSERT_EQ(r2.layer_2_ir_stream.status, DGTL_LAYER_PASS);
    INDUSTRY_ASSERT_EQ(r1.logic_hash, r2.logic_hash);

    dgtl_debug_free_report(&r1);
    dgtl_debug_free_report(&r2);
}

#ifndef DGTL_COMBINED_SUITE
int main() {
    return dgtl::test::IndustryTestRunner::instance().run_all();
}
#endif
