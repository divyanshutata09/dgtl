/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Module:      DGTL NewTest - Tier 11: Production Optimizer Algebraic Identities Gate
 * Owner:       Divyanshu Tata
 */

#include "industry_test_harness.h"
#include "dgtl_sdk/dgtl.h"

INDUSTRY_TEST(tier11_toolchain_opt_algebraic_laws) {
    // Tests mathematical axioms and algebraic laws:
    // Identity: x + 0 == x, x * 1 == x, x ^ 0 == x
    // Annihilator: x * 0 == 0
    // Self-Inverse: x ^ x == 0, x - x == 0
    // Idempotent: x & x == x, x | x == x
    // Double Negation: -(-x) == x, ~(~x) == x
    const char* src = 
        "fn main() -> i64 {\n"
        "    let x: i64 = 987654321;\n"
        "    let a: i64 = x + 0;\n"
        "    let b: i64 = a * 1;\n"
        "    let c: i64 = b ^ 0;\n"
        "    let d: i64 = c & c;\n"
        "    let e: i64 = d | d;\n"
        "    let f: i64 = -(-e);\n"
        "    let g: i64 = ~(~f);\n"
        "    let check: i64 = (g - x) + (x ^ x);\n"
        "    return check;\n" // Must fold completely to constant 0
        "}\n";

    dgtl_debug_options_t opts;
    dgtl_debug_default_options(&opts, DGTL_LANG_MYZON);
    opts.opt_level = 3; // Maximum optimization pipeline
    opts.run_smt_verifier = true; // Formal proof

    auto report = dgtl_debug_verify_entire_stack(src, &opts);
    INDUSTRY_ASSERT_EQ(report.layer_4_ir_passes.status, DGTL_LAYER_PASS);
    INDUSTRY_ASSERT_EQ(report.layer_3_smt_formal.status, DGTL_LAYER_PASS);
    INDUSTRY_ASSERT_EQ(report.layer_12_jit_execution.status, DGTL_LAYER_PASS);
    INDUSTRY_ASSERT_TRUE(report.dgtl_is_ok);
    INDUSTRY_ASSERT_EQ(report.dgtl_exit_code, 42);

    dgtl_debug_free_report(&report);
}

INDUSTRY_TEST(tier11_toolchain_opt_strength_reduction) {
    // Tests strength reduction passes:
    // Multiplication by powers of 2 -> shift left
    // Division by powers of 2 -> shift right
    // Modulo by powers of 2 -> bitwise AND
    const char* src = 
        "fn main() -> i64 {\n"
        "    let v: i64 = 1024;\n"
        "    let mul_shift: i64 = v * 8;\n"       // v << 3 = 8192
        "    let div_shift: i64 = mul_shift / 16;\n" // 8192 >> 4 = 512
        "    let mod_mask: i64 = div_shift % 64;\n"  // 512 & 63 = 0
        "    return mod_mask;\n"
        "}\n";

    dgtl_debug_options_t opts;
    dgtl_debug_default_options(&opts, DGTL_LANG_MYZON);
    opts.opt_level = 2;

    auto report = dgtl_debug_verify_entire_stack(src, &opts);
    INDUSTRY_ASSERT_EQ(report.layer_4_ir_passes.status, DGTL_LAYER_PASS);
    INDUSTRY_ASSERT_TRUE(report.dgtl_is_ok);
    INDUSTRY_ASSERT_EQ(report.dgtl_exit_code, 42);

    dgtl_debug_free_report(&report);
}

#ifndef DGTL_COMBINED_SUITE
int main() {
    return dgtl::test::IndustryTestRunner::instance().run_all();
}
#endif
