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

void run_test_pipeline_13_memory_smash() {
    constexpr size_t kArenaMbPerThread = 2;
    constexpr size_t kReservedNodes = 64;
    constexpr uint32_t kStorePtr = 100;
    constexpr uint32_t kLoadPtr = 200;
    constexpr uint64_t kStoreRegion = 1;
    constexpr uint64_t kLoadRegion = 2;

    std::cout << "--- [DGTL-TEST] 13_memory_smash ---\n";

    // Keep this test deterministic: 1 thread, explicit per-thread MB size.
    dgtl::core::DGTL_Arena arena(1, kArenaMbPerThread);
    dgtl::ir::IRStream stream(arena, kReservedNodes);

    dgtl::core::DGTLFormalVerifier verifier;
    dgtl::analysis::DGTLAICostModel model;
    dgtl::analysis::DGTLMemorySmasher smasher(verifier, model);

    smasher.register_ptr_region(kStorePtr, kStoreRegion);
    smasher.register_ptr_region(kLoadPtr, kLoadRegion);

    stream.begin_transaction();
    stream.push_node(dgtl::ir::OpCode::STORE, dgtl::ir::Flags::NONE, 1, kStorePtr, 0, 0, 0);
    stream.push_node(dgtl::ir::OpCode::LOAD, dgtl::ir::Flags::NONE, 2, kLoadPtr, 0, 0, 0);
    stream.commit_transaction();

    const bool reorder_ok = smasher.can_reorder(stream, 0, 1);
    if (stream.size() == 2 && reorder_ok) {
        std::cout << "SUCCESS\n";
        return;
    }

    std::cout << "FAIL\n";
    return;
}








