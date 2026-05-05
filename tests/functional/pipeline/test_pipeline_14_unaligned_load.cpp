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

void run_test_pipeline_14_unaligned_load() {
    constexpr size_t kArenaMbPerThread = 2;
    constexpr size_t kReservedNodes = 128;
    constexpr uint32_t kUnalignedOffset = 3;

    std::cout << "--- [DGTL-TEST] 14_unaligned_load ---\n";

    // Keep this test deterministic: 1 thread, explicit per-thread MB size.
    dgtl::core::DGTL_Arena arena(1, kArenaMbPerThread);
    dgtl::ir::IRStream stream(arena, kReservedNodes);

    stream.begin_transaction();
    // Simulate an unaligned byte offset load.
    stream.push_node(dgtl::ir::OpCode::LOAD, dgtl::ir::Flags::NONE, 1, 1000, 0, kUnalignedOffset, 0);
    stream.push_node(dgtl::ir::OpCode::ADD_I32, dgtl::ir::Flags::NONE, 2, 1, 1, 0, 0);
    stream.push_node(dgtl::ir::OpCode::RET, dgtl::ir::Flags::IS_RETURN, 0, 2, 0, 0, 0);
    stream.commit_transaction();

    const size_t before = stream.size();
    dgtl::pass::DGTLGVN gvn;
    gvn.execute(stream);

    const bool size_stable = stream.size() == before;
    const bool integrity_ok = stream.validate_shadow_integrity();

    if (size_stable && integrity_ok) {
        std::cout << "SUCCESS\n";
        return;
    }

    std::cout << "FAIL\n";
    return;
}








