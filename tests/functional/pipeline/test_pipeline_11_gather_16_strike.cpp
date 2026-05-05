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

void run_test_pipeline_11_gather_16_strike() {
    constexpr size_t kArenaMbPerThread = 2;
    constexpr size_t kReservedNodes = 64;
    constexpr dgtl::ir::Flags kGatherFlags =
        static_cast<dgtl::ir::Flags>(dgtl::ir::Flags::IS_SIMD_BURST | dgtl::ir::Flags::REQUIRES_VEC_PORT);

    std::cout << "--- [DGTL-TEST] 11_gather_16_strike ---\n";

    // Keep this test deterministic: 1 thread, explicit per-thread MB size.
    dgtl::core::DGTL_Arena arena(1, kArenaMbPerThread);
    dgtl::ir::IRStream stream(arena, kReservedNodes);

    stream.begin_transaction();
    stream.push_node(dgtl::ir::OpCode::GATHER_16,
                     kGatherFlags,
                     10, 1, 2, 0, 0);
    stream.push_node(dgtl::ir::OpCode::RET, dgtl::ir::Flags::IS_RETURN, 0, 10, 0, 0, 0);
    stream.commit_transaction();

    dgtl::pass::DGTLGVN gvn;
    gvn.execute(stream);

    dgtl::isa::DGTLNasmEmitter emitter;
    dgtl::core::SymbolTable symbols(arena);
    emitter.emit(stream, symbols);

    const auto asm_out = emitter.get_output();
    const auto n0 = stream.get_node(0);

    if (stream.size() == 2 &&
        n0.opcode == dgtl::ir::OpCode::GATHER_16 &&
        (n0.flags & dgtl::ir::Flags::IS_SIMD_BURST) &&
        (n0.flags & dgtl::ir::Flags::REQUIRES_VEC_PORT) &&
        !asm_out.empty()) {
        std::cout << "SUCCESS\n";
        return;
    }

    std::cout << "FAIL\n";
    return;
}








