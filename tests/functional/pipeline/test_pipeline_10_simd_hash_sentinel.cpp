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

void run_test_pipeline_10_simd_hash_sentinel() {
    constexpr size_t kArenaMbPerThread = 2;
    constexpr size_t kReservedNodes = 2048;
    constexpr dgtl::ir::Flags kRequiredFlags =
        static_cast<dgtl::ir::Flags>(dgtl::ir::Flags::IS_SIMD_BURST);

    std::cout << "--- [DGTL-TEST] 10_simd_512_alignment ---\n";

    // Keep this test deterministic: 1 thread, explicit per-thread MB size.
    dgtl::core::DGTL_Arena arena(1, kArenaMbPerThread);
    dgtl::ir::IRStream stream(arena, kReservedNodes);

    stream.begin_transaction();
    
    // Test a SIMD Burst with Vector Port requirements
    stream.push_node(
        dgtl::ir::OpCode::GATHER_F32_512, 
        kRequiredFlags,
        0x10, 0, 0, 0, 0
    );
    
    stream.commit_transaction();

    if (stream.size() == 1) {
        const auto node = stream.get_node(0);
        if (node.opcode == dgtl::ir::OpCode::GATHER_F32_512 &&
            (node.flags & dgtl::ir::Flags::IS_SIMD_BURST) &&
            (node.logic_flags & dgtl::ir::HintFlags::RequiresVecPort)) {
            std::cout << "SUCCESS\n";
            return;
        }
    }

    std::cout << "FAIL: SIMD hash sentinel node mismatch.\n";
    return;
}




