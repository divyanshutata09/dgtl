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

void run_test_pipeline_0() {
    constexpr size_t kReservedNodes = 1024;

    std::cout << "\033[1;36m--- [DGTL] Pipeline Integration Test ---\033[0m\n";

    // 1 thread slot, 1 MB per thread is sufficient for this test payload.
    dgtl::core::DGTL_Arena arena(1, 1);
    dgtl::ir::IRStream stream(arena, kReservedNodes);
    stream.set_count(0); 

    // --- TRANSACTION START ---
    // Injecting 3 nodes: Two identical ADDs and one SYSCALL anchor
    // FIX: Renamed begin_strike to begin_transaction to match IRStream implementation
    stream.begin_transaction(); 

    // Node 0: r1001 = 10 + 11
    stream.push_node(dgtl::ir::OpCode::ADD_I32, dgtl::ir::Flags::NONE, 1001, 10, 11, 0);
    
    // Node 1: r1002 = 10 + 11 (Duplicate of Node 0)
    stream.push_node(dgtl::ir::OpCode::ADD_I32, dgtl::ir::Flags::NONE, 1002, 10, 11, 0);

    // Node 2: SYSCALL using r1002. 
    // GVN will rewrite this to use r1001 and mark Node 1 as IS_DEAD.
    stream.push_node(dgtl::ir::OpCode::SYSCALL, dgtl::ir::Flags::NONE, 0, 1002, 0, 0);

    // FIX: Renamed commit_strike to commit_transaction
    stream.commit_transaction();
    // --- TRANSACTION END ---

    size_t final_count = stream.size();
    std::cout << "[Step 1] IR Injected. Initial Count: " << final_count << "\n";

    if (final_count < 3) {
        std::cout << "[FATAL] Stream integrity check failed. Nodes lost during injection.\n";
        return;
    }

    // --- [PILLAR 2] Apply BIR 1.0.0 GVN Logic ---
    // We call the internal stream method to ensure side-effect safety
    stream.perform_gvn(); 

    std::cout << "[Step 2] GVN Applied. Inspecting Stream:\n";
    
    int active_ops = 0;
    for(size_t i = 0; i < stream.size(); ++i) {
        auto node = stream.get_node(i); 
        // A node is active if it's not a NOP and doesn't carry the IS_DEAD flag
        bool is_active = (node.opcode != dgtl::ir::OpCode::NOP && !(node.flags & dgtl::ir::Flags::IS_DEAD));
        
        if(is_active) active_ops++;

        // Detailed node inspection
        std::cout << "  Node " << i << ": [" << (int)node.opcode << "] "
                  << "Res: " << node.result_id << ", "
                  << "OpA: " << node.op_a << ", "
                  << "Flags: " << (int)node.flags 
                  << (is_active ? " (ACTIVE)" : " (CLEANED)") << "\n";
    }
    
    std::cout << "[Result] Active Ops (Expected 2): " << active_ops << "\n";

    // --- NASM Emission ---
    dgtl::isa::DGTLNasmEmitter nasm;
    dgtl::core::SymbolTable symbols(arena);
    
    // The emitter now processes the optimized stream
    nasm.emit(stream, symbols);
    
    std::cout << "\n--- NASM OUTPUT ---\n" << nasm.get_output() << "\n";

    if (active_ops != 2) {
        std::cout << "\033[1;31m[ERROR] Pipeline Optimization Mismatch. Expected 2 active nodes.\033[0m\n";
        return;
    }

    std::cout << "\033[1;32m[SUCCESS] GVN merged identical nodes and preserved side-effecting SYSCALL.\033[0m\n";
    return;
}








