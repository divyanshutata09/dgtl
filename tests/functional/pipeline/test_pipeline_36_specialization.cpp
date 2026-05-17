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

void run_test_pipeline_36_specialization() {
    constexpr size_t kArenaMbPerThread = 2;
    constexpr size_t kReservedNodes = 1024;
    constexpr uint64_t kGuardTag = 0xBEEFULL;

    std::cout << "--- [DGTL-TEST] 36_jit_specialization ---\n";

    // Keep this test deterministic: 1 thread, explicit per-thread MB size.
    dgtl::core::DGTL_Arena arena(1, kArenaMbPerThread);
    dgtl::ir::IRStream stream(arena, kReservedNodes);

    stream.begin_transaction();
    
    // Push a guard node that models specialization/deoptimization gate metadata.
    stream.push_node(
        dgtl::ir::OpCode::GUARD_VALUE, 
        dgtl::ir::HintFlags::RequiresDepassTarget, 
        0, 0, 0, 0, kGuardTag
    );
    
    stream.commit_transaction();

    if (stream.size() == 1) {
        const auto node = stream.get_node(0);
        if (node.opcode == dgtl::ir::OpCode::GUARD_VALUE &&
            (node.flags & dgtl::ir::HintFlags::RequiresDepassTarget) &&
            node.intent_hash == kGuardTag) {
            std::cout << "SUCCESS\n";
            return;
        }
    }

    std::cout << "FAIL\n";
    return;
}




