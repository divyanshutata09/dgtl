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

void run_test_pipeline_06_quad_add_fusion() {
    constexpr size_t kReservedNodes = 16;
    constexpr dgtl::ir::OpCode kExpectedFusedOpcode = static_cast<dgtl::ir::OpCode>(0x204);

    // Keep this test lightweight and deterministic: 1 thread, 2 MB arena.
    dgtl::core::DGTL_Arena arena(1, 2);
    dgtl::ir::IRStream stream(arena, kReservedNodes);
    stream.begin_transaction();
    for (uint32_t i = 0u; i < 4u; ++i) {
        stream.push_node(dgtl::ir::OpCode::ADD_I32, dgtl::ir::Flags::NONE, 100u + i, i, i, 0u);
    }
    stream.commit_transaction();

    dgtl::ir::pass::DGTLSIMDLegalizer legalizer(dgtl::ir::pass::TargetArch::X86_AVX512);
    legalizer.legalize(stream);

    if (stream.get_node(0).opcode == kExpectedFusedOpcode) {
        std::cout << "SUCCESS";
    }

    return;
}








