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


void run_test_pipeline_15_transactional_rollback() {
    constexpr size_t kArenaMbPerThread = 2;
    constexpr size_t kReservedNodes = 1024;

    std::cout << "--- [DGTL-TEST] 15_rollback_gate ---\n";

    // Keep this test deterministic: 1 thread, explicit per-thread MB size.
    dgtl::core::DGTL_Arena arena(1, kArenaMbPerThread);
    dgtl::ir::IRStream stream(arena, kReservedNodes);

    const size_t start_size = stream.size();

    stream.begin_transaction();
    stream.push_node(dgtl::ir::OpCode::STORE, dgtl::ir::Flags::NONE, 0, 100, 200, 0, 0);
    
    // FAIL THE TRANSACTION
    stream.rollback_transaction();

    if (stream.size() == start_size) {
        std::cout << "SUCCESS\n";
    } else {
        std::cout << "FAIL: Stream size changed after rollback!\n";
        return;
    }
    return;
}




