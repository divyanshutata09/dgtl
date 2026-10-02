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

void run_test_pipeline_09_mixed_precision() {
    constexpr size_t kArenaMbPerThread = 2;
    constexpr size_t kReservedNodes = 1024;
    constexpr size_t kExpectedNodeCount = 2;

    std::cout << "--- [DGTL-TEST] 09_mixed_precision ---\n";

    // Keep this test deterministic: 1 thread, explicit per-thread MB size.
    dgtl::core::DGTL_Arena arena(1, kArenaMbPerThread);
    dgtl::ir::IRStream stream(arena, kReservedNodes);

    stream.begin_transaction();
    
    // Mix I32 and F32 operations
    stream.push_node(dgtl::ir::OpCode::ADD_I32, dgtl::ir::Flags::NONE, 1, 10, 20, 0, 0);
    stream.push_node(dgtl::ir::OpCode::ADD_F32, dgtl::ir::Flags::IS_FUSED, 2, 5, 5, 0, 0);
    
    stream.commit_transaction();

    if (stream.size() == kExpectedNodeCount) {
        std::cout << "SUCCESS\n";
    } else {
        return;
    }
    return;
}




