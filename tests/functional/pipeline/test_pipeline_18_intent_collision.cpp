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

#include "dgtl/dgtl_all.hpp"

#include <iostream>

void run_test_pipeline_18_intent_collision() {
    constexpr uint32_t kResult = 1;
    constexpr uint32_t kOpA = 10;
    constexpr uint32_t kOpB = 20;
    constexpr uint32_t kOpC = 0;

    std::cout << "--- [DGTL-TEST] 18_intent_collision ---\n";
    
    // Ensure operand order affects the canonical DGTL intent hash.
    const uint64_t hash1 = dgtl::ir::IRNode::calculate_hash(dgtl::ir::OpCode::ADD_I32,
                                                            kResult,
                                                            kOpA,
                                                            kOpB,
                                                            kOpC);
    const uint64_t hash2 = dgtl::ir::IRNode::calculate_hash(dgtl::ir::OpCode::ADD_I32,
                                                            kResult,
                                                            kOpB,
                                                            kOpA,
                                                            kOpC);

    if (hash1 != hash2) {
        std::cout << "[opt] Intent hash differentiates operand order.\n";
        std::cout << "SUCCESS\n";
    } else {
        std::cout << "[FAIL] Hash collision on operand swap!\n";
    }
    
    return;
}




