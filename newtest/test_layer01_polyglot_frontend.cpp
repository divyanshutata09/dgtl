/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Module:      DGTL NewTest - Layer 1: Polyglot Frontend Ingestion Test
 * Owner:       Divyanshu Tata
 */

#include "industry_test_harness.h"
#include "dgtl_sdk/dgtl.h"

INDUSTRY_TEST(layer01_frontend_myzon_ingestion) {
    const char* myzon_src = 
        "fn calculate() -> i32 {\n"
        "    let x = 100;\n"
        "    let y = 200;\n"
        "    return x + y;\n"
        "}\n";

    dgtl_debug_options_t opts;
    dgtl_debug_default_options(&opts, DGTL_LANG_MYZON);
    opts.opt_level = 0;

    auto report = dgtl_debug_verify_entire_stack(myzon_src, &opts);
    INDUSTRY_ASSERT_EQ(report.layer_1_frontend.status, DGTL_LAYER_PASS);
    INDUSTRY_ASSERT_TRUE(report.ir_node_count > 0);
    dgtl_debug_free_report(&report);
}

INDUSTRY_TEST(layer01_frontend_textual_ir_ingestion) {
    const char* textual_ir = 
        "; DGTL Canonical Textual IR\n"
        "op LOAD_IMM 1 500 0 4294967295 1\n"
        "op LOAD_IMM 2 600 0 4294967295 1\n"
        "op ADD_I32  3 1   2 4294967295 0\n"
        "op RET      4294967295 3 4294967295 4294967295 2\n";

    dgtl_debug_options_t opts;
    dgtl_debug_default_options(&opts, DGTL_LANG_IR_TEXT);

    auto report = dgtl_debug_verify_entire_stack(textual_ir, &opts);
    INDUSTRY_ASSERT_EQ(report.layer_1_frontend.status, DGTL_LAYER_PASS);
    INDUSTRY_ASSERT_TRUE(report.ir_node_count >= 4);
    dgtl_debug_free_report(&report);
}

INDUSTRY_TEST(layer01_frontend_empty_source_rejection) {
    dgtl_debug_options_t opts;
    dgtl_debug_default_options(&opts, DGTL_LANG_MYZON);

    auto report = dgtl_debug_verify_entire_stack("", &opts);
    INDUSTRY_ASSERT_EQ(report.layer_1_frontend.status, DGTL_LAYER_FAIL);
    INDUSTRY_ASSERT_EQ(report.verdict, DGTL_DEBUG_FRONTEND_ERROR);
    INDUSTRY_ASSERT_EQ(report.first_failing_layer_idx, 1);
    INDUSTRY_ASSERT_NOT_NULL(report.layer_1_frontend.error_message);
    dgtl_debug_free_report(&report);
}

#ifndef DGTL_COMBINED_SUITE
int main() {
    return dgtl::test::IndustryTestRunner::instance().run_all();
}
#endif
