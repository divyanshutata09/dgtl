/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Module:      DGTL NewTest - Tier 11: Production Multicall Dispatcher Gate
 * Owner:       Divyanshu Tata
 */

#include "industry_test_harness.h"
#include <string>
#include <string_view>
#include <vector>
#include <algorithm>

namespace {

std::string normalize_name(std::string_view name) {
    std::string s(name);
    if (s.ends_with(".exe") || s.ends_with(".EXE")) {
        s.erase(s.size() - 4);
    }
    std::replace(s.begin(), s.end(), '_', '-');
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

} // namespace

INDUSTRY_TEST(tier11_toolchain_multicall_name_resolution) {
    // Tests multicall name normalization across all aliases:
    // e.g. 'dgtl-link.exe' -> 'dgtl-link'
    // 'dgtl_opt' -> 'dgtl-opt'
    INDUSTRY_ASSERT_EQ(normalize_name("dgtl-link.exe"), "dgtl-link");
    INDUSTRY_ASSERT_EQ(normalize_name("dgtl_link.exe"), "dgtl-link");
    INDUSTRY_ASSERT_EQ(normalize_name("dgtl-opt"), "dgtl-opt");
    INDUSTRY_ASSERT_EQ(normalize_name("DGTL_LLC.EXE"), "dgtl-llc");
    INDUSTRY_ASSERT_EQ(normalize_name("dgtl-tools.exe"), "dgtl-tools");
    INDUSTRY_ASSERT_EQ(normalize_name("dgtl_release.exe"), "dgtl-release");
}

#ifndef DGTL_COMBINED_SUITE
int main() {
    return dgtl::test::IndustryTestRunner::instance().run_all();
}
#endif
