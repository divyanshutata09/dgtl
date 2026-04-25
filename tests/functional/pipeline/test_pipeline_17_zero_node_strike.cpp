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

/* test_pipeline_17_zero_node_strike.cpp */
#include <iostream>
#include "dgtl/core/Arena.hpp"
#include "dgtl/ir/Global_IR.hpp"

void run_test_pipeline_17_zero_node_strike() {
    std::cout << "--- [DGTL-TEST] 17_zero_node_strike ---" << std::endl;
    dgtl::core::DGTL_Arena arena(512);
    dgtl::ir::IRStream stream(arena, 1024);

    size_t before = stream.size();
    stream.begin_transaction();
    // No nodes pushed
    stream.commit_transaction();
    size_t after = stream.size();

    if (before == after) {
        std::cout << "SUCCESS" << std::endl;
    } else {
        std::cout << "FAIL: Stream size changed despite zero nodes." << std::endl;
        return;
    }
    return;
}




