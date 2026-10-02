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

void run_test_pipeline_21_dual_bank() {
    constexpr size_t kArenaMbPerThread = 2;
    constexpr size_t kReservedNodes = 16;
    constexpr dgtl::ir::OpCode kNeonVectorOpcode = static_cast<dgtl::ir::OpCode>(0x304);

    // Keep this test deterministic: 1 thread, explicit per-thread MB size.
    dgtl::core::DGTL_Arena arena(1, kArenaMbPerThread);
    dgtl::ir::IRStream stream(arena, kReservedNodes);
    
    // Use the real framework components (no mocks).
    dgtl::analysis::LivenessAnalyzer liveness;
    dgtl::isa::DGTLARM64RegAlloc allocator;

    stream.begin_transaction();
    
    // 1. Scalar Op (Should get GPR, e.g., X0)
    stream.push_node(dgtl::ir::OpCode::ADD_I32, dgtl::ir::Flags::NONE, 10, 1, 2, 0);
    
    // 2. SIMD Op (Should get NEON, e.g., V0)
    // Use legalizer-style SIMD tag to route allocation toward vector banks.
    stream.push_node(kNeonVectorOpcode, dgtl::ir::Flags::IS_SIMD_BURST, 20, 11, 12, 0);

    stream.commit_transaction();
    
    // Note: If your real liveness.is_live_at() defaults to true, this works perfectly.
    allocator.execute(stream, liveness);

    const auto scalar_node = stream.get_node(0);
    const auto vector_node = stream.get_node(1);

    // op_c stores the physical register index after execute()
    bool scalar_ok = (scalar_node.op_c < 31);
    bool vector_ok = (vector_node.op_c < 32); 

    std::cout << "Scalar Phys Reg: X" << static_cast<int>(scalar_node.op_c) << "\n";
    std::cout << "Vector Phys Reg: V" << static_cast<int>(vector_node.op_c) << "\n";

    if (scalar_ok && vector_ok) {
        std::cout << "SUCCESS\n";
    }

    return;
}








