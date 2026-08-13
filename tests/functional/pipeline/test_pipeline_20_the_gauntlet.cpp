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

void run_test_pipeline_20_the_gauntlet() {
    constexpr size_t kArenaMbPerThread = 4;
    constexpr size_t kReservedNodes = 128;
    constexpr size_t kExpectedInitialNodeCount = 6;

    std::cout << "--- [DGTL-TEST] 20_the_gauntlet ---\n";

    // Keep this test deterministic: 1 thread, explicit per-thread MB size.
    dgtl::core::DGTL_Arena arena(1, kArenaMbPerThread);
    dgtl::ir::IRStream stream(arena, kReservedNodes);

    try {
        stream.begin_transaction();

        // Build a compact mixed workload: duplicate expression, SIMD gather, and side-effect anchor.
        stream.push_node(dgtl::ir::OpCode::ADD_I32, dgtl::ir::Flags::NONE, 100, 10, 11, 0, 0);
        stream.push_node(dgtl::ir::OpCode::ADD_I32, dgtl::ir::Flags::NONE, 101, 10, 11, 0, 0);
        stream.push_node(dgtl::ir::OpCode::GATHER_16,
                         static_cast<uint16_t>(dgtl::ir::Flags::IS_SIMD_BURST),
                         102,
                         20,
                         21,
                         0,
                         0);
        stream.push_node(dgtl::ir::OpCode::STORE,
                         static_cast<uint16_t>(dgtl::ir::Flags::HAS_SIDE_EFFECTS),
                         0,
                         200,
                         102,
                         0,
                         0);
        stream.push_node(dgtl::ir::OpCode::SYSCALL, dgtl::ir::Flags::NONE, 0, 101, 0, 0, 0);
        stream.push_node(dgtl::ir::OpCode::RET, dgtl::ir::Flags::IS_RETURN, 0, 100, 0, 0, 0);

        stream.commit_transaction();

        if (stream.size() != kExpectedInitialNodeCount) {
            std::cout << "FAIL: Unexpected initial node count.\n";
            return;
        }

        const size_t before = stream.size();

        dgtl::pass::DGTLGVN gvn;
        gvn.execute(stream);
        stream.perform_dead_code_elimination();

        const bool non_empty = stream.size() > 0;
        const bool reduced_or_same = stream.size() <= before;
        const bool integrity_ok = stream.validate_shadow_integrity();
        const auto tail = stream.get_node(stream.size() - 1);
        const bool terminates = tail.opcode == dgtl::ir::OpCode::RET;

        if (non_empty && reduced_or_same && integrity_ok && terminates) {
            std::cout << "GAUNTLET STATUS: SUCCESS\n";
            return;
        }

        std::cout << "GAUNTLET STATUS: FAIL\n";
    } catch (const std::exception& e) {
        std::cerr << "GAUNTLET CRASHED: " << e.what() << '\n';
        return;
    }

    return;
}




