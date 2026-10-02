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


void run_ghost_test() {
    constexpr size_t kArenaMbPerThread = 5;
    constexpr size_t kReservedNodes = 1000;
    constexpr size_t kGhostNodes = 100;

    // Initialize Arena and Stream
    dgtl::core::DGTL_Arena arena(1, kArenaMbPerThread);
    dgtl::ir::IRStream stream(arena, kReservedNodes);

    std::cout << "\033[1;36m[DGTL-GHOST]\033[0m Testing Transactional Atomicity...\n";

    // 1. Commit a valid Base Node
    stream.begin_transaction();
    stream.push_node(dgtl::ir::OpCode::ADD_I32, dgtl::ir::Flags::NONE, 1, 10, 10, 0);
    stream.commit_transaction();

    // 2. Start a "Ghost" Transaction and then ABORT
    stream.begin_transaction();
    for (size_t i = 0; i < kGhostNodes; ++i) {
        stream.push_node(dgtl::ir::OpCode::ADD_I32, dgtl::ir::Flags::NONE, 500 + i, 1, 1, 0);
    }
    
    // Using the internal fixed abort instead of manual set_count
    stream.abort_transaction(); 

    // 3. Start a new "Clean" Transaction
    stream.begin_transaction();
    stream.push_node(dgtl::ir::OpCode::SYSCALL, dgtl::ir::Flags::NONE, 2, 1, 0, 0);
    stream.commit_transaction();

    const size_t final_size = stream.size();
    std::cout << "[INFO] Stream Size after Ghost Abort: " << final_size << "\n";

    // Verification
    if (final_size == 2) {
        const auto node2 = stream.get_node(1);
        if (node2.opcode == dgtl::ir::OpCode::SYSCALL) {
            std::cout << "\033[1;32m[opt]\033[0m Transactional integrity verified. Ghost nodes purged.\n";
        } else {
            std::cout << "\033[1;31m[FAIL]\033[0m Opcode mismatch at index 1. Got: " << static_cast<int>(node2.opcode) << "\n";
        }
    } else {
        std::cout << "\033[1;31m[FAIL]\033[0m Stream leaked " << (final_size - 2) << " ghost nodes!\n";
    }

    return;
}




