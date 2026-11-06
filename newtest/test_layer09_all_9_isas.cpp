/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Module:      DGTL NewTest - Layer 9: 9-ISA Codegen Matrix Test
 * Owner:       Divyanshu Tata
 */

#include "industry_test_harness.h"
#include "dgtl_sdk/dgtl.h"

INDUSTRY_TEST(layer09_all_9_isas_cross_compilation) {
    const char* src = 
        "fn main() -> i32 {\n"
        "    let a = 20;\n"
        "    let b = 22;\n"
        "    return a + b;\n"
        "}\n";

    dgtl_debug_options_t opts;
    dgtl_debug_default_options(&opts, DGTL_LANG_MYZON);

    dgtl_entire_audit_report_t reports[9];
    bool ok = dgtl_debug_verify_all_9_isas(src, &opts, reports);

    const char* kArchNames[9] = {
        "x86_64", "ARM64", "WASM", "RISC-V 64", "ARM32", "CUDA PTX", "ROCm AMDGPU", "FPGA", "SPIR-V"
    };

    for (size_t i = 0; i < 9; ++i) {
        INDUSTRY_ASSERT_EQ(reports[i].layer_9_isa_codegen.status, DGTL_LAYER_PASS);
        INDUSTRY_ASSERT_EQ(reports[i].layer_10_object_file.status, DGTL_LAYER_PASS);
        dgtl_debug_free_report(&reports[i]);
    }

    INDUSTRY_ASSERT_TRUE(ok);
}

#ifndef DGTL_COMBINED_SUITE
int main() {
    return dgtl::test::IndustryTestRunner::instance().run_all();
}
#endif
