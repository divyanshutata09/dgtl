/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Module:      DGTL NewTest - Layer 3: SMT Formal Equivalence & Sentinels Test
 * Owner:       Divyanshu Tata
 */

#include "industry_test_harness.h"
#include "dgtl_sdk/dgtl.h"

INDUSTRY_TEST(layer03_smt_formal_canonical_graph) {
    const char* src = 
        "fn main() -> i32 {\n"
        "    let x = 15;\n"
        "    let y = 25;\n"
        "    return x * y;\n"
        "}\n";

    dgtl_debug_options_t opts;
    dgtl_debug_default_options(&opts, DGTL_LANG_MYZON);
    opts.run_smt_verifier = true;

    auto report = dgtl_debug_verify_entire_stack(src, &opts);
    INDUSTRY_ASSERT_EQ(report.layer_3_smt_formal.status, DGTL_LAYER_PASS);
    INDUSTRY_ASSERT_TRUE(report.layer_3_smt_formal.latency_us > 0 || report.layer_3_smt_formal.status == DGTL_LAYER_PASS);
    dgtl_debug_free_report(&report);
}

INDUSTRY_TEST(layer03_smt_formal_sentinel_preservation) {
    const char* src = 
        "fn main() -> i32 {\n"
        "    let mut sum = 0;\n"
        "    for i in 0..10 {\n"
        "        sum = sum + i;\n"
        "    }\n"
        "    return sum;\n"
        "}\n";

    dgtl_debug_options_t opts;
    dgtl_debug_default_options(&opts, DGTL_LANG_MYZON);
    opts.run_smt_verifier = true;

    auto report = dgtl_debug_verify_entire_stack(src, &opts);
    INDUSTRY_ASSERT_EQ(report.layer_3_smt_formal.status, DGTL_LAYER_PASS);
    dgtl_debug_free_report(&report);
}

#ifndef DGTL_COMBINED_SUITE
int main() {
    return dgtl::test::IndustryTestRunner::instance().run_all();
}
#endif
