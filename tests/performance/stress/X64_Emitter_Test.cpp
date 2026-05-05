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

#include <iomanip>
#include <iostream>
#include <vector>

void print_hex(const std::vector<uint8_t>& code) {
    std::cout << "--- Generated Machine Code (" << code.size() << " bytes) ---\n";
    for (size_t i = 0; i < code.size(); ++i) {
        std::cout << std::hex << std::setw(2) << std::setfill('0') 
                  << static_cast<int>(code[i]) << " ";
        if ((i + 1) % 8 == 0) std::cout << "  ";
        if ((i + 1) % 16 == 0) std::cout << "\n";
    }
    std::cout << std::dec << "\n\n";
}

void run_X64_Emitter_Test() {
    dgtl::isa::X64StrikeEmitter emitter;
    
    // FIX: Explicitly initialize the stream with a capacity (e.g., 1024 nodes)
    // opting nullptr as the first argument triggers internal allocation.
    dgtl::ir::IRStream stream(nullptr, 1024);
    
    std::vector<uint8_t> machine_code;

    std::cout << "--- Starting DGTL Strike Test (BIR 1.0.0 x64) ---\n";

    // 1. LOAD_IMM: RAX = 0x1122334455667788
    dgtl::ir::IRNode n1{};
    n1.opcode = dgtl::ir::OpCode::LOAD_IMM;
    n1.flags = dgtl::ir::Flags::NONE;
    n1.result_id = 0;      // RAX
    n1.op_a = 0x11223344; 
    n1.op_b = 0x55667788; 
    n1.op_c = dgtl::ir::INVALID_SLOT;
    stream.push_back(n1);

    // 2. ADD_I32: RAX += RCX
    dgtl::ir::IRNode n2{};
    n2.opcode = dgtl::ir::OpCode::ADD_I32;
    n2.flags = dgtl::ir::Flags::NONE;
    n2.result_id = 0;      // Dest: RAX
    n2.op_a = 0;           // Src A: RAX
    n2.op_b = 1;           // Src: RCX
    n2.op_c = dgtl::ir::INVALID_SLOT;
    stream.push_back(n2);

    // 3. REG_MOVE: RDX = RAX
    dgtl::ir::IRNode n3{};
    n3.opcode = dgtl::ir::OpCode::REG_MOVE;
    n3.flags = dgtl::ir::Flags::NONE;
    n3.result_id = 2;      // Dest: RDX
    n3.op_a = 0;           // Src: RAX
    n3.op_b = dgtl::ir::INVALID_SLOT;
    n3.op_c = dgtl::ir::INVALID_SLOT;
    stream.push_back(n3);

    // 4. RET
    dgtl::ir::IRNode n4{};
    n4.opcode = dgtl::ir::OpCode::RET;
    n4.flags = dgtl::ir::Flags::NONE;
    n4.result_id = dgtl::ir::INVALID_SLOT;
    n4.op_a = 0;
    n4.op_b = dgtl::ir::INVALID_SLOT;
    n4.op_c = dgtl::ir::INVALID_SLOT;
    stream.push_back(n4);

    std::cout << "Stream size: " << stream.size() << " nodes.\n";

    // Run Emission
    emitter.process_stream(stream, machine_code);

    if (machine_code.empty()) {
        std::cout << "[!] FAIL: Emitter returned 0 bytes.\n";
        return;
    }

    print_hex(machine_code);
    std::cout << "[SUCCESS] DGTL x64 Strike Test Complete.\n";

    return;
}







