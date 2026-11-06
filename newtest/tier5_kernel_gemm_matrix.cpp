/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Module:      DGTL NewTest - Tier 5: Real Dense GEMM 4x4 Matrix Multiplication Test
 * Owner:       Divyanshu Tata
 */

#include "industry_test_harness.h"
#include "dgtl_sdk/dgtl.h"

INDUSTRY_TEST(tier5_gemm_dense_4x4_matrix_multiply_and_trace) {
    // Real 4x4 GEMM Matrix Multiplication C = A * B:
    // A: Identity-diagonal shifted matrix
    // B: Linear ramp matrix
    // Computes C[i][j] = sum_k(A[i][k] * B[k][j])
    // Computes Trace(C) = C[0][0] + C[1][1] + C[2][2] + C[3][3]
    // Mathematical exact Trace = 40
    const char* src = 
        "fn main() -> i32 {\n"
        "    let mut a = [\n"
        "        2, 0, 0, 0,\n"
        "        0, 2, 0, 0,\n"
        "        0, 0, 2, 0,\n"
        "        0, 0, 0, 2\n"
        "    ];\n"
        "    let mut b = [\n"
        "        1, 2, 3, 4,\n"
        "        5, 6, 7, 8,\n"
        "        9, 10, 11, 12,\n"
        "        13, 14, 15, 16\n"
        "    ];\n"
        "    let mut c = [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0];\n"
        "    for i in 0..4 {\n"
        "        for j in 0..4 {\n"
        "            let mut dot = 0;\n"
        "            for k in 0..4 {\n"
        "                dot = dot + (a[i * 4 + k] * b[k * 4 + j]);\n"
        "            }\n"
        "            c[i * 4 + j] = dot;\n"
        "        }\n"
        "    }\n"
        "    // C = 2 * B\n"
        "    // Trace(C) = 2*(1 + 6 + 11 + 16) = 2*34 = 68\n"
        "    let trace = c[0] + c[5] + c[10] + c[15];\n"
        "    if trace != 68 { return 1; }\n"
        "    // Element check: C[0][1] = 2 * B[0][1] = 2 * 2 = 4\n"
        "    if c[1] != 4 { return 2; }\n"
        "    // Element check: C[3][3] = 2 * B[3][3] = 2 * 16 = 32\n"
        "    if c[15] != 32 { return 3; }\n"
        "    return 0;\n"
        "}\n";

    dgtl_debug_options_t opts;
    dgtl_debug_default_options(&opts, DGTL_LANG_MYZON);
    opts.opt_level = 3;

    auto report = dgtl::debug::verify_against_golden(src, "", 0, DGTL_LANG_MYZON, 3);
    INDUSTRY_ASSERT_TRUE(report.is_ok());
    INDUSTRY_ASSERT_EQ(report.exit_code(), 0);
}

#ifndef DGTL_COMBINED_SUITE
int main() {
    return dgtl::test::IndustryTestRunner::instance().run_all();
}
#endif
