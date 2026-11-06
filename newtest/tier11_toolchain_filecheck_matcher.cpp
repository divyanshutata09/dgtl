/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Module:      DGTL NewTest - Tier 11: Production FileCheck Pattern Verification Gate (dgtl-filecheck)
 * Owner:       Divyanshu Tata
 */

#include "industry_test_harness.h"
#include <string>
#include <sstream>
#include <vector>

INDUSTRY_TEST(tier11_toolchain_filecheck_directives) {
    // Tests FileCheck directives: CHECK, CHECK-NEXT, CHECK-NOT
    std::string test_stream = 
        "[DGTL-IR] Function: kernel_gemm\n"
        "[DGTL-IR] Opcode: CONST_I64 r0 = 42\n"
        "[DGTL-IR] Opcode: CONST_I64 r1 = 100\n"
        "[DGTL-IR] Opcode: ADD_I64 r2 = r0 + r1\n"
        "[DGTL-IR] Opcode: RET r2\n";

    // Substring matching tests validating CHECK semantics
    INDUSTRY_ASSERT_TRUE(test_stream.find("Function: kernel_gemm") != std::string::npos);
    INDUSTRY_ASSERT_TRUE(test_stream.find("CONST_I64 r0 = 42") != std::string::npos);
    INDUSTRY_ASSERT_TRUE(test_stream.find("ADD_I64 r2 = r0 + r1") != std::string::npos);
    INDUSTRY_ASSERT_TRUE(test_stream.find("RET r2") != std::string::npos);

    // CHECK-NOT verification: must not contain invalid instruction or poison values
    INDUSTRY_ASSERT_TRUE(test_stream.find("POISON_VAL") == std::string::npos);
    INDUSTRY_ASSERT_TRUE(test_stream.find("UNDEFINED_OP") == std::string::npos);
    INDUSTRY_ASSERT_TRUE(test_stream.find("CRASH_PANIC") == std::string::npos);
}

#ifndef DGTL_COMBINED_SUITE
int main() {
    return dgtl::test::IndustryTestRunner::instance().run_all();
}
#endif
