/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Module:      DGTL NewTest - Tier 12: Internal Quality Tooling - Duplication Gate (dgtl-dup-gate)
 * Owner:       Divyanshu Tata
 */

#include "industry_test_harness.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <sstream>
#include <algorithm>

namespace {

// Rabin-Karp / rolling token hash window detector modeled after dgtl-dup-gate
uint64_t hash_token_sequence(const std::vector<std::string>& tokens, size_t start, size_t window) {
    uint64_t h = 14695981039346656037ULL; // FNV-1a offset basis
    for (size_t i = 0; i < window && (start + i) < tokens.size(); ++i) {
        for (char c : tokens[start + i]) {
            h ^= static_cast<uint64_t>(static_cast<unsigned char>(c));
            h *= 1099511628211ULL;
        }
    }
    return h;
}

std::vector<std::string> tokenize_pseudo_cxx(const std::string& code) {
    std::vector<std::string> toks;
    std::string cur;
    for (char c : code) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            if (!cur.empty()) {
                toks.push_back(cur);
                cur.clear();
            }
        } else if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') {
            cur += c;
        } else {
            if (!cur.empty()) {
                toks.push_back(cur);
                cur.clear();
            }
            toks.push_back(std::string(1, c));
        }
    }
    if (!cur.empty()) toks.push_back(cur);
    return toks;
}

} // namespace

INDUSTRY_TEST(tier12_tool_dup_gate_token_window_detection) {
    std::string unit_a = 
        "uint64_t compute_matrix_determinant(double* mat, size_t n) {\n"
        "    if (n == 1) return mat[0];\n"
        "    if (n == 2) return mat[0]*mat[3] - mat[1]*mat[2];\n"
        "    double det = 0.0;\n"
        "    for (size_t i = 0; i < n; ++i) { det += mat[i] * cofactor(mat, n, i); }\n"
        "    return det;\n"
        "}";

    std::string unit_b = 
        "uint64_t cloned_matrix_determinant(double* mat, size_t n) {\n"
        "    if (n == 1) return mat[0];\n"
        "    if (n == 2) return mat[0]*mat[3] - mat[1]*mat[2];\n"
        "    double det = 0.0;\n"
        "    for (size_t i = 0; i < n; ++i) { det += mat[i] * cofactor(mat, n, i); }\n"
        "    return det;\n"
        "}";

    std::string unit_c = 
        "void execute_instruction_stream(const Instruction* stream, size_t count) {\n"
        "    for (size_t k = 0; k < count; ++k) {\n"
        "        dispatch_op(stream[k].opcode, stream[k].args);\n"
        "    }\n"
        "}";

    auto toks_a = tokenize_pseudo_cxx(unit_a);
    auto toks_b = tokenize_pseudo_cxx(unit_b);
    auto toks_c = tokenize_pseudo_cxx(unit_c);

    INDUSTRY_ASSERT_TRUE(toks_a.size() >= 30);
    INDUSTRY_ASSERT_TRUE(toks_b.size() >= 30);

    // Check sliding window collision between unit_a and cloned unit_b (starting past function name)
    const size_t window_size = 20;
    std::unordered_map<uint64_t, size_t> window_hashes;
    for (size_t i = 5; i + window_size <= toks_a.size(); ++i) {
        window_hashes[hash_token_sequence(toks_a, i, window_size)] = i;
    }

    size_t duplicate_windows_detected = 0;
    for (size_t i = 5; i + window_size <= toks_b.size(); ++i) {
        uint64_t h = hash_token_sequence(toks_b, i, window_size);
        if (window_hashes.contains(h)) {
            duplicate_windows_detected++;
        }
    }
    INDUSTRY_ASSERT_TRUE(duplicate_windows_detected > 0);

    // Verify unit_c has zero duplicate windows against unit_a
    size_t false_positives = 0;
    for (size_t i = 0; i + window_size <= toks_c.size(); ++i) {
        uint64_t h = hash_token_sequence(toks_c, i, window_size);
        if (window_hashes.contains(h)) {
            false_positives++;
        }
    }
    INDUSTRY_ASSERT_EQ(false_positives, 0u);
}

#ifndef DGTL_COMBINED_SUITE
int main() {
    return dgtl::test::IndustryTestRunner::instance().run_all();
}
#endif
