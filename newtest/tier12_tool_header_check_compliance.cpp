/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Module:      DGTL NewTest - Tier 12: Internal Quality Tooling - Header Compliance Check (dgtl-header-check)
 * Owner:       Divyanshu Tata
 */

#include "industry_test_harness.h"
#include <string>
#include <sstream>
#include <vector>

INDUSTRY_TEST(tier12_tool_header_compliance_verification) {
    const std::string required_header = "Copyright (c) 2026 TataG Systems. All rights reserved.";

    std::string valid_source =
        "/*\n"
        " * Copyright (c) 2026 TataG Systems. All rights reserved.\n"
        " *\n"
        " * Project: DGTL\n"
        " */\n"
        "#include <iostream>\n";

    std::string missing_notice_source =
        "// Open-source style header without proper copyright notice\n"
        "#include <vector>\n";

    auto check_header = [&](const std::string& code) -> bool {
        std::istringstream iss(code);
        std::string line;
        int count = 0;
        while (std::getline(iss, line) && count++ < 12) {
            if (line.find(required_header) != std::string::npos) {
                return true;
            }
        }
        return false;
    };

    INDUSTRY_ASSERT_TRUE(check_header(valid_source));
    INDUSTRY_ASSERT_FALSE(check_header(missing_notice_source));
}

#ifndef DGTL_COMBINED_SUITE
int main() {
    return dgtl::test::IndustryTestRunner::instance().run_all();
}
#endif
