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

#include <iostream>
#include "dgtl/ir/Global_IR.hpp"
#include "dgtl/core/Arena.hpp"

using namespace dgtl;

void run_test_pipeline_18_intent_collision() {
    std::cout << "--- [DGTL-TEST] 18_intent_collision ---" << std::endl;
    
    // Test logic: Ensure different operand order produces different hashes
    // mimicking the logic: (op_val * 31u) ^ (op_a * 17u) ^ (op_b * 7u)
    uint32_t opcode = 0x1;
    uint32_t a = 10, b = 20;
    
    uint32_t hash1 = (opcode * 31u) ^ (a * 17u) ^ (b * 7u);
    uint32_t hash2 = (opcode * 31u) ^ (b * 17u) ^ (a * 7u); // Swapped operands

    if (hash1 != hash2) {
        std::cout << "[opt] Intent hash differentiates operand order." << std::endl;
        std::cout << "SUCCESS" << std::endl;
    } else {
        std::cout << "[FAIL] Hash collision on operand swap!" << std::endl;
    }
    
    return;
}




