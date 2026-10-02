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

import dgtl;
import dgtl.arena;
import dgtl.ir.stream;
import dgtl.ir_enums;
import dgtl.ir_node;

#include <iostream>


void run_test_pipeline_08_partial_window() {
    constexpr size_t kArenaMbPerThread = 2;
    constexpr size_t kReservedNodes = 1024;
    constexpr size_t kScratchU64Count = 128;

    std::cout << "--- [DGTL-TEST] 08_partial_window ---\n";

    // Keep this test deterministic: 1 thread, explicit per-thread MB size.
    dgtl::core::DGTL_Arena arena(1, kArenaMbPerThread);
    
    // Simulate a partial arena window touch.
    (void)arena.allocate<uint64_t>(kScratchU64Count);

    dgtl::ir::IRStream stream(arena, kReservedNodes);
    size_t pre_size = stream.size();
    
    stream.begin_transaction();

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

    if (stream.size() > pre_size) {
        std::cout << "SUCCESS\n";
    } else {
        std::cout << "FAIL: Stream size did not increase after push_node.\n";
        return;
    }

    return;
}




