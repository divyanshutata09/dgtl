/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Module:      DGTL NewTest - Tier 11: Production LLC Multi-Target Codegen Gate
 * Owner:       Divyanshu Tata
 */

#include "industry_test_harness.h"
#include "dgtl_sdk/dgtl.h"

INDUSTRY_TEST(tier11_toolchain_llc_all_isa_matrix) {
    // Tests dgtl-llc across all 9 supported target ISAs
    const char* src = 
        "fn main() -> i32 {\n"
        "    let a: i32 = 100;\n"
        "    let b: i32 = 200;\n"
        "    return a + b;\n"
        "}\n";

    const dgtl_target_arch_t targets[] = {
        DGTL_ARCH_X86_64,
        DGTL_ARCH_ARM64,
        DGTL_ARCH_WASM,
        DGTL_ARCH_RISCV64,
        DGTL_ARCH_ARM32,
        DGTL_ARCH_CUDA,
        DGTL_ARCH_ROCM,
        DGTL_ARCH_FPGA,
        DGTL_ARCH_SPIRV
    };

    for (const auto target : targets) {
        dgtl_debug_options_t opts;
        dgtl_debug_default_options(&opts, DGTL_LANG_MYZON);
        opts.target_arch = target;
        opts.opt_level = 2;

        auto report = dgtl_debug_verify_entire_stack(src, &opts);
        INDUSTRY_ASSERT_EQ(report.layer_6_mir_isel.status, DGTL_LAYER_PASS);
        INDUSTRY_ASSERT_EQ(report.layer_9_isa_codegen.status, DGTL_LAYER_PASS);
        INDUSTRY_ASSERT_TRUE(report.layer_9_isa_codegen.latency_us >= 0);
        dgtl_debug_free_report(&report);
    }
}

#ifndef DGTL_COMBINED_SUITE
int main() {
    return dgtl::test::IndustryTestRunner::instance().run_all();
}
#endif
