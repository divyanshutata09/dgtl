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

#include <iostream>
#include <iomanip>
#include <vector>
#include "dgtl/isa/X64_Strike_Emitter.hpp"
#include "dgtl/ir/Global_IR.hpp"

using namespace dgtl;

void print_hex(const std::vector<uint8_t>& code) {
    std::cout << "--- Generated Machine Code (" << code.size() << " bytes) ---" << std::endl;
    for (size_t i = 0; i < code.size(); ++i) {
        std::cout << std::hex << std::setw(2) << std::setfill('0') 
                  << static_cast<int>(code[i]) << " ";
        if ((i + 1) % 8 == 0) std::cout << "  ";
        if ((i + 1) % 16 == 0) std::cout << std::endl;
    }
    std::cout << std::dec << "\n" << std::endl;
}

void run_X64_Emitter_Test() {
    dgtl::isa::X64StrikeEmitter emitter;
    
    // FIX: Explicitly initialize the stream with a capacity (e.g., 1024 nodes)
    // opting nullptr as the first argument triggers internal allocation.
    ir::IRStream stream(nullptr, 1024); 
    
    std::vector<uint8_t> machine_code;

    std::cout << "--- Starting DGTL Strike Test (BIR 2.1 x64) ---\n";

    // 1. LOAD_IMM: RAX = 0x1122334455667788
    ir::IRNode n1;
    n1.opcode = ir::OpCode::LOAD_IMM;
    n1.result_id = 0;      // RAX
    n1.op_a = 0x11223344; 
    n1.op_b = 0x55667788; 
    stream.push_back(n1);

    // 2. ADD_I32: RAX += RCX
    ir::IRNode n2;
    n2.opcode = ir::OpCode::ADD_I32;
    n2.result_id = 0;      // Dest: RAX
    n2.op_b = 1;           // Src: RCX
    stream.push_back(n2);

    // 3. REG_MOVE: RDX = RAX
    ir::IRNode n3;
    n3.opcode = ir::OpCode::REG_MOVE;
    n3.result_id = 2;      // Dest: RDX
    n3.op_a = 0;           // Src: RAX
    stream.push_back(n3);

    // 4. RET
    ir::IRNode n4;
    n4.opcode = ir::OpCode::RET;
    stream.push_back(n4);

    std::cout << "Stream size: " << stream.size() << " nodes." << std::endl;

    // Run Emission
    emitter.process_stream(stream, machine_code);

    if (machine_code.empty()) {
        std::cout << "[!] FAIL: Emitter returned 0 bytes." << std::endl;
        return;
    }

    print_hex(machine_code);
    std::cout << "[SUCCESS] DGTL x64 Strike Test Complete." << std::endl;

    return;
}






