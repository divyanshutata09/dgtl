/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Module:      DGTL NewTest - Tier 11: Production Formal Verifier SMT Theorems Gate
 * Owner:       Divyanshu Tata
 */

#include "industry_test_harness.h"
#include "dgtl_sdk/dgtl.h"

INDUSTRY_TEST(tier11_toolchain_verify_smt_de_morgan) {
    // Tests SMT Formal Verifier on Boolean De Morgan Theorem:
    // ~(A | B) == (~A & ~B)
    const char* src = 
        "fn main() -> i64 {\n"
        "    let a: i64 = 123456789;\n"
        "    let b: i64 = 987654321;\n"
        "    let lhs: i64 = ~(a | b);\n"
        "    let rhs: i64 = (~a) & (~b);\n"
        "    let diff: i64 = lhs ^ rhs;\n"
        "    return diff;\n" // Must equal 0 identically
        "}\n";

    dgtl_debug_options_t opts;
    dgtl_debug_default_options(&opts, DGTL_LANG_MYZON);
    opts.run_smt_verifier = true;
    opts.opt_level = 2;

    auto report = dgtl_debug_verify_entire_stack(src, &opts);
    INDUSTRY_ASSERT_EQ(report.layer_3_smt_formal.status, DGTL_LAYER_PASS);
    INDUSTRY_ASSERT_TRUE(report.layer_3_smt_formal.latency_us >= 0);
    INDUSTRY_ASSERT_EQ(report.dgtl_exit_code, 0);

    dgtl_debug_free_report(&report);
}

INDUSTRY_TEST(tier11_toolchain_verify_smt_twos_complement) {
    // Tests SMT Formal Verifier on Two's Complement Negation Axiom:
    // -x == ~x + 1
    const char* src = 
        "fn main() -> i64 {\n"
        "    let x: i64 = 42424242;\n"
        "    let neg_x: i64 = -x;\n"
        "    let comp_add: i64 = (~x) + 1;\n"
        "    let diff: i64 = neg_x - comp_add;\n"
        "    return diff;\n" // Must equal 0 identically
        "}\n";

    dgtl_debug_options_t opts;
    dgtl_debug_default_options(&opts, DGTL_LANG_MYZON);
    opts.run_smt_verifier = true;

    auto report = dgtl_debug_verify_entire_stack(src, &opts);
    INDUSTRY_ASSERT_EQ(report.layer_3_smt_formal.status, DGTL_LAYER_PASS);
    INDUSTRY_ASSERT_EQ(report.dgtl_exit_code, 0);

    dgtl_debug_free_report(&report);
}

#ifndef DGTL_COMBINED_SUITE
int main() {
    return dgtl::test::IndustryTestRunner::instance().run_all();
}
#endif
