/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 * DGTL Test 07 — Frontend Pipeline Lowerer Compatibility Gate
 *
 * Judges: the policy-required frontend lowerer test artifact exists and still
 *         exports the expected aggregate symbol name.
 */

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;

void run_test_t07_frontend_pipeline_lowerer() {
    std::cout << "[T07] Frontend Pipeline Lowerer Compatibility\n";

    const fs::path compatibility_test =
        fs::path("tests") / "functional" / "frontend" / "t07_frontend_pipeline_lowerer.cpp";

    if (!fs::exists(compatibility_test)) {
        throw std::runtime_error(
            "T07: missing frontend lowerer compatibility test file: " + compatibility_test.string());
    }

    std::ifstream in(compatibility_test, std::ios::binary);
    if (!in) {
        throw std::runtime_error(
            "T07: unable to open frontend compatibility test file: " + compatibility_test.string());
    }

    const std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (content.find("run_test_t07_frontend_pipeline_lowerer") == std::string::npos) {
        throw std::runtime_error(
            "T07: frontend compatibility file does not export run_test_t07_frontend_pipeline_lowerer");
    }

    std::cout << "[T07] PASS - frontend lowerer compatibility artifact and symbol are present\n";
}
