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

#include <cstdio>
#include <iostream>
#include <string>

import dgtl_all;
import dgtl.ir_core.semantic;
import dgtl.ir_core.transform_scalar_core;

int main() {
    std::cout << "\033[1;36m--- [DGTL] Pipeline Integration Test ---\033[0m\n";

    // 1. PILLAR 1: Initialize the huge-page arena (1 thread, 256MB).
    // Ensures physical continuity for Tier 1 and Tier 2 IR layouts.
    dgtl::core::DGTL_Arena arena(1, 256);
    if (arena.raw_data() == nullptr) {
        printf("[FATAL] OS failed to provide Huge-Page Arena.\n");
        return -1;
    }

    // 2. INITIALIZE STREAM
    // Sets up the Bit-Packed IR Stream using the provided Arena.
    constexpr size_t MAX_NODES = 1024;
    dgtl::ir::IRStream stream(arena, MAX_NODES);
    stream.set_count(0); 

    std::cout << "[DEBUG] Stream Capacity: " << stream.capacity() << " nodes\n";

    // 3. TRANSACTIONAL INJECTION (Pillar 2)
    // begin_transaction() opens the transaction window for low-latency injection.
    stream.begin_transaction(); 

    // Node 0: r1001 = 10 + 11
    stream.push_node(dgtl::ir::OpCode::ADD_I32, dgtl::ir::Flags::NONE, 1001, 10, 11, 0);
    
    // Node 1: r1002 = 10 + 11 (Redundant entry for GVN testing)
    stream.push_node(dgtl::ir::OpCode::ADD_I32, dgtl::ir::Flags::NONE, 1002, 10, 11, 0);

    /* * THE ANCHOR: 
     * Using r1001 in a RET instruction. 
     * This marks r1001 as "live," allowing GVN to eliminate r1002 while 
     * preserving the original logic.
     */
    stream.push_node(dgtl::ir::OpCode::RET, dgtl::ir::Flags::NONE, 0, 1001, 0, 0);

    std::cout << "[DEBUG] Transaction Status: " << (stream.is_active_transaction() ? "ACTIVE" : "INACTIVE") << "\n";

    // 4. COMMIT STRIKE
    // Finalizes the memory layout and prepares the stream for optimization.
    stream.commit_transaction();

    std::cout << "[Step 1] IR Injected. Initial Count: " << stream.size() << "\n";

    // 5. RUN GLOBAL VALUE NUMBERING (GVN)
    if (stream.size() > 0) {
        dgtl::pass::transform::DGTLGVN gvn;
        gvn.execute(stream); 
    } else {
        std::cout << "[ERROR] Pipeline Stalled: IR Stream is empty.\n";
        return 1;
    }

    // 6. VERIFY OPTIMIZATION
    int active_ops = 0;
    std::cout << "[DEBUG] Post-GVN Stream Analysis:\n";
    
    for(size_t i = 0; i < stream.size(); ++i) {
        // Retrieve unified IRNode view (24-byte alignment)
        const auto node = stream.get_node(i);
        
        bool is_nop = (node.opcode == dgtl::ir::OpCode::NOP);
        bool is_dead = (node.flags & dgtl::ir::Flags::IS_DEAD);

        printf("  Node[%03zu]: Op=%04X, Result=%u, Dead=%s, Nop=%s\n", 
                static_cast<unsigned long>(i), 
                static_cast<uint16_t>(node.opcode), 
                node.result_id, 
                is_dead ? "YES" : "NO", 
                is_nop ? "YES" : "NO");

        if(!is_nop && !is_dead) {
            active_ops++;
        }
    }
    
    // Logic: Node 0 (ADD) and Node 2 (RET) should remain. Node 1 (ADD) is pruned.
    std::cout << "[Step 2] GVN Applied. Active Ops (Expected 2): " << active_ops << "\n";

    // 7. EMIT NASM ASSEMBLY
    dgtl::isa::DGTLNasmEmitter nasm;
    dgtl::semantic::SymbolTable symbols(arena);

    std::cout << "\n--- GENERATING NASM OUTPUT ---\n";
    try {
        nasm.emit(stream, symbols);
        std::string assembly = nasm.get_output();

        if (assembly.empty() || assembly.length() < 10) {
            std::cout << "; [WARNING] Assembly output suspiciously small. Check emitter logic.\n";
        } else {
            std::cout << assembly << "\n";
        }
    } catch (const std::exception& e) {
        std::cerr << "[FATAL] Emission failed: " << e.what() << "\n";
    }

    std::cout << "\n--- [TEST COMPLETE] ---\n";

    return 0;
}
