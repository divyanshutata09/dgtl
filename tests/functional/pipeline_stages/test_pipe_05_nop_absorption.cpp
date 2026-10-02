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

void run_test_pipeline_05_nop_absorption() {
    constexpr size_t kReservedNodes = 16;

    // Keep this test lightweight and deterministic: 1 thread, 2 MB arena.
    dgtl::core::DGTL_Arena arena(1, 2);
    dgtl::ir::IRStream stream(arena, kReservedNodes);
    stream.push_node(dgtl::ir::OpCode::NOP, dgtl::ir::Flags::NONE, 0, 0, 0, 0);

    if (stream.get_node(0).opcode == dgtl::ir::OpCode::NOP) {
        std::cout << "SUCCESS";
    }

    return;
}




