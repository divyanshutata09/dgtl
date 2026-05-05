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

void run_test_pipeline_04_cross_block_ssa() {
    constexpr size_t kReservedNodes = 16;

    // Keep this test lightweight and deterministic: 1 thread, 2 MB arena.
    dgtl::core::DGTL_Arena arena(1, 2);
    dgtl::ir::IRStream stream(arena, kReservedNodes);
    
    // Testing the result_id storage for register 1001
    stream.push_node(dgtl::ir::OpCode::ADD_I32, dgtl::ir::Flags::NONE, 1001, 10, 20, 0);
    
    const auto node = stream.get_node(0);
    
    // Using the 'result_id' field as defined in IRNode
    if (node.result_id == 1001) {
        std::cout << "SUCCESS";
    }
    
    return;
}




