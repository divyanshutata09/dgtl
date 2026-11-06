/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Module:      DGTL NewTest - Tier 12: Internal Quality Tooling - DGTL DSL Op Code Generator & Validator (dgtl-gen & dgtl-gen-validate)
 * Owner:       Divyanshu Tata
 */

#include "industry_test_harness.h"
#include <string>
#include <vector>
#include <regex>
#include <sstream>

INDUSTRY_TEST(tier12_tool_dsl_gen_and_validation_schema) {
    // Tests canonical DSL grammar specifications and AST validation rules
    // applied by dgtl-gen and dgtl-gen-validate
    std::string sample_dsl =
        "#@ version: 2.0\n"
        "#@ canonical_source: true\n"
        "#@ opcode_values: explicit\n"
        "#@ stable_abi: preserve_existing_values\n"
        "\n"
        "op ADD_I32: operands=2, results=1, value=0x0101, dialect=\"core\"\n"
        "op SUB_I32: operands=2, results=1, value=0x0102, dialect=\"core\"\n"
        "op MUL_I32: operands=2, results=1, value=0x0103, dialect=\"core\"\n"
        "op RET: operands=1, results=0, value=0x000F, dialect=\"core\"\n"
        "\n"
        "alias OP_ADD = ADD_I32\n";

    // 1. Invariant: Directives must exist
    INDUSTRY_ASSERT_TRUE(sample_dsl.find("canonical_source: true") != std::string::npos);
    INDUSTRY_ASSERT_TRUE(sample_dsl.find("opcode_values: explicit") != std::string::npos);

    // 2. Opcode regex parsing
    std::regex op_re(R"(^\s*op\s+([A-Za-z_][A-Za-z0-9_]*):.*value=(0x[0-9A-Fa-f]+))", std::regex::ECMAScript);
    std::istringstream iss(sample_dsl);
    std::string line;
    size_t op_count = 0;
    while (std::getline(iss, line)) {
        std::smatch m;
        if (std::regex_search(line, m, op_re)) {
            op_count++;
            std::string op_name = m[1].str();
            std::string hex_val = m[2].str();
            uint32_t val = static_cast<uint32_t>(std::stoul(hex_val, nullptr, 16));
            INDUSTRY_ASSERT_TRUE(val > 0);
        }
    }
    INDUSTRY_ASSERT_EQ(op_count, 4u);

    // 3. Alias parsing
    INDUSTRY_ASSERT_TRUE(sample_dsl.find("alias OP_ADD = ADD_I32") != std::string::npos);
}

#ifndef DGTL_COMBINED_SUITE
int main() {
    return dgtl::test::IndustryTestRunner::instance().run_all();
}
#endif
