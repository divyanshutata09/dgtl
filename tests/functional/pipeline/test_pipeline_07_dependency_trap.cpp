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

void run_test_pipeline_07_dependency_trap() {
    constexpr size_t kReservedNodes = 16;
    constexpr int kExpectedActiveNodes = 2;

    // Keep this test lightweight and deterministic: 1 thread, 2 MB arena.
    dgtl::core::DGTL_Arena arena(1, 2);
    dgtl::ir::IRStream stream(arena, kReservedNodes);
    stream.begin_transaction();
    stream.push_node(dgtl::ir::OpCode::ADD_I32, dgtl::ir::Flags::NONE, 1, 10, 11, 0);
    stream.push_node(dgtl::ir::OpCode::ADD_I32, dgtl::ir::Flags::NONE, 2, 1, 13, 0);
    stream.commit_transaction();

    dgtl::pass::DGTLSIMDLegalizer legalizer(dgtl::pass::TargetArch::X86_AVX512);
    legalizer.legalize(stream);

    int active = 0;
    for (size_t i = 0; i < stream.size(); ++i) {
        if (stream.get_node(i).opcode != dgtl::ir::OpCode::NOP) {
            ++active;
        }
    }

    if (active == kExpectedActiveNodes) {
        std::cout << "SUCCESS";
    }

    return;
}








