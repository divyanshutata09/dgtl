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

void run_test_pipeline_17_zero_node_strike() {
    constexpr size_t kArenaMbPerThread = 2;
    constexpr size_t kReservedNodes = 1024;

    std::cout << "--- [DGTL-TEST] 17_zero_node_strike ---\n";

    // Keep this test deterministic: 1 thread, explicit per-thread MB size.
    dgtl::core::DGTL_Arena arena(1, kArenaMbPerThread);
    dgtl::ir::IRStream stream(arena, kReservedNodes);

    const size_t before = stream.size();
    stream.begin_transaction();
    // No nodes pushed
    stream.commit_transaction();
    const size_t after = stream.size();

    if (before == after) {
        std::cout << "SUCCESS\n";
    } else {
        std::cout << "FAIL: Stream size changed despite zero nodes.\n";
        return;
    }
    return;
}




