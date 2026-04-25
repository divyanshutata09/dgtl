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
#include <cassert>

#include "dgtl/core/Arena.hpp"
#include "dgtl/ir/Global_IR.hpp"  


void run_test_pipeline_08_partial_window() {
    std::cout << "--- [DGTL-TEST] 08_partial_window ---" << std::endl;

    // 1. Initialize Arena
    dgtl::core::DGTL_Arena arena(256); 
    
    // 2. Simulate Partial Window
    // Use (void) to silence the [[nodiscard]] warning correctly
    (void)arena.allocate<uint64_t>(128); 

    // 3. Initialize IRStream
    dgtl::ir::IRStream stream(arena, 1024);
    size_t pre_size = stream.size();
    
    // 4. Execute Transaction
    stream.begin_transaction();
    
    // FIX: Removed 'uint32_t id =' because your push_node returns void
    stream.push_node(
        dgtl::ir::OpCode::ADD_I32, 
        dgtl::ir::Flags::NONE,     
        1,                         
        10,                        
        20,                        
        0,                         
        0                          
    );
    
    stream.commit_transaction();

    // 5. Verify logic
    // We check if the stream size increased, which proves the node was added.
    if (stream.size() > pre_size) {
        std::cout << "SUCCESS" << std::endl;
    } else {
        std::cout << "FAIL: Stream size did not increase after push_node." << std::endl;
        return;
    }

    return;
}




