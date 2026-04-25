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

#include "dgtl/core/Arena.hpp"
#include "dgtl/ir/Global_IR.hpp"


void run_test_pipeline_15_transactional_rollback() {
    std::cout << "--- [DGTL-TEST] 15_rollback_gate ---" << std::endl;
    dgtl::core::DGTL_Arena arena(512); 
    dgtl::ir::IRStream stream(arena, 1024);

    size_t start_size = stream.size();

    stream.begin_transaction();
    stream.push_node(dgtl::ir::OpCode::STORE, dgtl::ir::Flags::NONE, 0, 100, 200, 0, 0);
    
    // FAIL THE TRANSACTION
    stream.rollback_transaction();

    // The size should have returned to start_size
    if (stream.size() == start_size) {
        std::cout << "SUCCESS" << std::endl;
    } else {
        std::cout << "FAIL: Stream size changed after rollback!" << std::endl;
        return;
    }
    return;
}




