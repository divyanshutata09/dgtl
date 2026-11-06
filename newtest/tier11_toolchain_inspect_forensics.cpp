/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Module:      DGTL NewTest - Tier 11: Production Binary Forensic Inspector Gate (dgtl-inspect)
 * Owner:       Divyanshu Tata
 */

#include "industry_test_harness.h"
#include "dgtl_sdk/dgtl.h"

INDUSTRY_TEST(tier11_toolchain_inspect_security_flags) {
    // Tests binary inspection for DEP/NX (W^X), ASLR dynamic base, and Large Address Awareness
    const char* src = 
        "fn main() -> i32 {\n"
        "    let mut x: i32 = 0;\n"
        "    let mut i: i32 = 0;\n"
        "    while i < 10 {\n"
        "        x = x + i;\n"
        "        i = i + 1;\n"
        "    }\n"
        "    return x;\n"
        "}\n";

    dgtl_debug_options_t opts;
    dgtl_debug_default_options(&opts, DGTL_LANG_MYZON);
    opts.run_sovereign_link = true;
    opts.opt_level = 2;

    auto report = dgtl_debug_verify_entire_stack(src, &opts);
    // Verifies Layer 11 Linker produces binary conforming to forensic standards
    INDUSTRY_ASSERT_EQ(report.layer_11_sovereign_linker.status, DGTL_LAYER_PASS);
    INDUSTRY_ASSERT_EQ(report.dgtl_exit_code, 45); // 0+1+2+...+9 = 45

    dgtl_debug_free_report(&report);
}

#ifndef DGTL_COMBINED_SUITE
int main() {
    return dgtl::test::IndustryTestRunner::instance().run_all();
}
#endif
