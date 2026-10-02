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

#include <intrin.h>
#include <iostream>

void run_fractal_torture_test() {
    constexpr size_t kArenaMbPerThread = 20;
    constexpr size_t kReservedNodes = 10000;
    constexpr size_t kFanoutBranches = 50;

    dgtl::core::DGTL_Arena arena(1, kArenaMbPerThread);
    dgtl::ir::IRStream stream(arena, kReservedNodes);
    dgtl::isa::DGTLNasmEmitter nasm;
    dgtl::core::SymbolTable symbols(arena);

    std::cout << "\033[1;36m[DGTL-FRACTAL]\033[0m Starting Non-Linear Dependency Torture...\n";

    stream.begin_transaction();
    
    // Create a "Root" node
    stream.push_node(dgtl::ir::OpCode::ADD_I32, dgtl::ir::Flags::NONE, 1000, 5, 5, 0); // r1000 = 10

    // Create 50 nodes that ALL depend on r1000
    // This tests the "Fan-out" stability of the Symbol Table
    for (size_t i = 0; i < kFanoutBranches; ++i) {
        stream.push_node(dgtl::ir::OpCode::ADD_I32, dgtl::ir::Flags::NONE, 2000 + i, 1000, i, 0);
    }

    // Create 50 nodes that are EXACT DUPLICATES of the previous 50
    // This is the ultimate test for the GVN Hash Map under pressure
    for (size_t i = 0; i < kFanoutBranches; ++i) {
        stream.push_node(dgtl::ir::OpCode::ADD_I32, dgtl::ir::Flags::NONE, 3000 + i, 1000, i, 0);
    }

    stream.commit_transaction();

    // Measure Optimization + Emission Latency
    uint64_t start = __rdtsc();
    stream.perform_gvn(); 
    nasm.emit(stream, symbols);
    uint64_t end = __rdtsc();

    const size_t total_nodes = stream.size();
    int active_nodes = 0;
    for (size_t i = 0; i < total_nodes; ++i) {
        if (!(stream.get_node(i).flags & dgtl::ir::Flags::IS_DEAD)) {
            ++active_nodes;
        }
    }

    std::cout << "\033[1;33m[RESULT]\033[0m Fractal GVN + Emission: " << (end - start) << " cycles\n";
    std::cout << "[INFO] Total Injected: " << total_nodes << " | Active after GVN: " << active_nodes << "\n";

    if (active_nodes <= 51) {
        std::cout << "\033[1;32m[opt]\033[0m GVN successfully collapsed 50 redundant fractal branches.\n";
    } else {
        std::cout << "\033[1;31m[FAIL]\033[0m Redundant nodes leaked through GVN.\n";
    }

    return;
}







