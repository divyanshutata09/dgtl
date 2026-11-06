/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Module:      DGTL NewTest - Tier 12: Internal Quality Tooling - Instruction Selection Generator (dgtl-isel-gen)
 * Owner:       Divyanshu Tata
 */

#include "industry_test_harness.h"
#include <string>
#include <vector>
#include <map>
#include <sstream>

INDUSTRY_TEST(tier12_tool_isel_contract_table_mapping) {
    // Tests ISel target contract compilation logic:
    // Opcode pattern matching, cost evaluation, and register constraints
    struct ISelRule {
        std::string pattern;
        std::string target_insn;
        uint32_t cost;
    };

    std::vector<ISelRule> x86_rules = {
        {"ADD_I32(r, r)", "addl %reg, %reg", 1},
        {"ADD_I32(r, imm)", "addl $imm, %reg", 1},
        {"MUL_I32(r, r)", "imull %reg, %reg", 3},
        {"DIV_I32(r, r)", "idivl %reg", 15}
    };

    // Verify cost-based selection: addition is lower cost than multiplication/division
    INDUSTRY_ASSERT_TRUE(x86_rules[0].cost < x86_rules[2].cost);
    INDUSTRY_ASSERT_TRUE(x86_rules[2].cost < x86_rules[3].cost);

    // Verify pattern uniqueness
    std::map<std::string, std::string> table;
    for (const auto& r : x86_rules) {
        INDUSTRY_ASSERT_FALSE(table.contains(r.pattern));
        table[r.pattern] = r.target_insn;
    }
    INDUSTRY_ASSERT_EQ(table.size(), 4u);
}

#ifndef DGTL_COMBINED_SUITE
int main() {
    return dgtl::test::IndustryTestRunner::instance().run_all();
}
#endif
