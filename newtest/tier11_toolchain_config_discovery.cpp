/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Module:      DGTL NewTest - Tier 11: Production SDK Discovery & Invariants Gate
 * Owner:       Divyanshu Tata
 */

#include "industry_test_harness.h"
#include "dgtl_sdk/dgtl.h"
#include <cstring>
#include <string>

INDUSTRY_TEST(tier11_toolchain_sdk_abi_invariants) {
    // Tests ABI Version, Semantic Version, and Sentinels
    dgtl_version_info_t vi = dgtl_get_version();
    INDUSTRY_ASSERT_EQ(vi.major, 2u);
    INDUSTRY_ASSERT_EQ(vi.minor, 1u);
    INDUSTRY_ASSERT_EQ(vi.patch, 0u);
    INDUSTRY_ASSERT_EQ(vi.abi_version, 4u);
    INDUSTRY_ASSERT_EQ(DGTL_VERSION_MAJOR, 2u);
    INDUSTRY_ASSERT_EQ(DGTL_ABI_VERSION, 4u);

    const char* ver_str = dgtl_version_string();
    INDUSTRY_ASSERT_TRUE(ver_str != nullptr);
    INDUSTRY_ASSERT_TRUE(std::strstr(ver_str, "2.1.0-SOVEREIGN") != nullptr);

    // Tests Target Architecture Availability
    INDUSTRY_ASSERT_TRUE(DGTL_ARCH_X86_64 == 0);
    INDUSTRY_ASSERT_TRUE(DGTL_ARCH_ARM64 == 1);
    INDUSTRY_ASSERT_TRUE(DGTL_ARCH_WASM == 3);
    INDUSTRY_ASSERT_TRUE(DGTL_ARCH_RISCV64 == 4);
    INDUSTRY_ASSERT_TRUE(dgtl_abi_compatible(4u));
}

#ifndef DGTL_COMBINED_SUITE
int main() {
    return dgtl::test::IndustryTestRunner::instance().run_all();
}
#endif
