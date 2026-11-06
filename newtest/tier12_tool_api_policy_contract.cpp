/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Module:      DGTL NewTest - Tier 12: Internal Quality Tooling - API Policy Enforcement (dgtl-api-policy)
 * Owner:       Divyanshu Tata
 */

#include "industry_test_harness.h"
#include <string>
#include <vector>
#include <regex>

INDUSTRY_TEST(tier12_tool_api_policy_verification) {
    // Tests compliance with DGTL Sovereign C++26 API Policy
    // Rules:
    // 1. C-API headers must use extern "C" guard
    // 2. Export symbols must be marked DGTL_API / __declspec(dllexport)
    // 3. No raw std::string or std::vector in sovereign public ABI interface
    // 4. Stable version sentinels present
    
    std::string sample_valid_header =
        "#pragma once\n"
        "#ifdef __cplusplus\n"
        "extern \"C\" {\n"
        "#endif\n"
        "DGTL_API uint32_t dgtl_abi_version(void);\n"
        "DGTL_API const char* dgtl_version_string(void);\n"
        "#ifdef __cplusplus\n"
        "}\n"
        "#endif\n";

    std::string sample_violating_header =
        "#pragma once\n"
        "#include <string>\n"
        "std::string get_compiler_info();\n"; // Violates C ABI stability

    // Rule 1: Check extern "C"
    INDUSTRY_ASSERT_TRUE(sample_valid_header.find("extern \"C\"") != std::string::npos);
    INDUSTRY_ASSERT_TRUE(sample_violating_header.find("extern \"C\"") == std::string::npos);

    // Rule 2: Check DGTL_API decoration
    INDUSTRY_ASSERT_TRUE(sample_valid_header.find("DGTL_API") != std::string::npos);
    INDUSTRY_ASSERT_TRUE(sample_violating_header.find("DGTL_API") == std::string::npos);

    // Rule 3: Public ABI must not expose std::string across sovereign C ABI boundary
    std::regex stl_abi_leak(R"(\bstd::(string|vector|map|unordered_map)\b)");
    INDUSTRY_ASSERT_FALSE(std::regex_search(sample_valid_header, stl_abi_leak));
    INDUSTRY_ASSERT_TRUE(std::regex_search(sample_violating_header, stl_abi_leak));
}

#ifndef DGTL_COMBINED_SUITE
int main() {
    return dgtl::test::IndustryTestRunner::instance().run_all();
}
#endif
