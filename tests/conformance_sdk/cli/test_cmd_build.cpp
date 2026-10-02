/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:    DGTL (Deep General-purpose Transformation Layer)
 * Owner:      Divyanshu Tata
 * Repository: github.com/TataG-Systems/DGTL
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

int main() {
    const fs::path orchestrator = fs::path("tools") / "cpp" / "dgtl_main.cpp";
    std::ifstream in(orchestrator, std::ios::binary);
    if (!in) {
        std::cerr << "[FAIL] BuildCommandTest: cannot read " << orchestrator.string() << "\n";
        return 1;
    }

    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const bool has_build = text.find("command == \"build\" || command == \"build-release\"") != std::string::npos;
    if (!has_build) {
        std::cerr << "[FAIL] BuildCommandTest: build route missing in orchestrator\n";
        return 1;
    }

    std::cout << "[PASS] BuildCommandTest\n";
    return 0;
}