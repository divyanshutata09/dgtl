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

#include "dgtl//runtime/_Executor.hpp"
#include "dgtl/isa/emitter/NASM_Emitter.hpp"

#include <iostream>
#include <vector>
#include <cassert>
#include <cstring>

void run_test_pipeline_20__tiering() {
    using namespace dgtl::ir;
    using namespace dgtl::runtime;

    DGTLExecutor (1); 
    dgtl::isa::DGTLNasmEmitter ;

    // 1. Create stream with capacity 1024, starting count 0
    IRStream stream(nullptr, 1024); 

    // 2. Injection with Explicit Register Mapping (BIR 1.0.0)
    // Node 0: IMMEDIATE -> VR1 (rbx). Value: 1337
    stream.push_node(OpCode::IMMEDIATE, 0, 1, 0, 1337, 0, 0); 
    
    // Node 1: ADD_I32 -> VR2 (rcx) = VR1 (rbx) + 1 (using VR index 1 as source)
    stream.push_node(OpCode::ADD_I32, 0, 2, 1, 1, 0, 0); 
    
    // Node 2: RET -> Return value from VR2 (rcx)
    stream.push_node(OpCode::RET, 0, 0, 2, 0, 0, 0);

    std::cout << "[DEBUG] Stream Count before : " << stream.size() << std::endl;

    // 3.  Mapping (Simulating a captured trace)
    std::vector<uint8_t> machine_code = { 
        0x48, 0xC7, 0xC0, 0x39, 0x05, 0x00, 0x00, 0x48, 0x01, 0xD8, 0xC3 
    };

    // load_and_prepare calls our fixed clone() internally
    auto func = .load_and_prepare(machine_code, stream);
    
    std::cout << "[DEBUG]  Function Pointer: " << (void*)func << std::endl;

    if (!func) {
        std::cout << "[DEBUG]  failed to return a valid function pointer!" << std::endl;
        return;
    }

    // --- PIPELINE PROCEEDS ---
    auto retrieved_ir = .get_ir_from_address(reinterpret_cast<uintptr_t>(func));
    
    // Diagnostic: Check if opcodes survived the Unified View Reconstruction
    bool data_loss = false;
    for(size_t i = 0; i < retrieved_ir.size(); ++i) {
        if (retrieved_ir.get_node(i).opcode == OpCode::NOP) {
            std::cerr << "[CRITICAL] Node " << i << " lost its OpCode during  mapping!" << std::endl;
            data_loss = true;
        }
    }

    if (data_loss) {
        std::cerr << "[FAIL] Check clone() implementation in BitPacked_IR.cpp" << std::endl;
        return;
    }

    std::cout << "[DGTL] Trace Retrieval Success. Nodes: " << retrieved_ir.size() << std::endl;

    dgtl::core::SymbolTable symbols;
    
    // Final  Bake
    .emit(retrieved_ir, symbols);
    
    std::string output = .get_output();
    
    if (output.empty()) {
        std::cerr << "[FAIL] Emitter produced empty output despite valid IR nodes!" << std::endl;
        return;
    }

    std::cout << "\n--- BAKED  ASM ---\n" << output << "------------------------\n";
    
    // Final Validation
    assert(retrieved_ir.size() == 3);
    
    if (output.find("1337") == std::string::npos) {
        std::cerr << "[FAIL] Immediate value 1337 not found in translated ASM!" << std::endl;
        return;
    }
    
    std::cout << "[TEST optED] -to- Bridge Verified." << std::endl;

    return;
}




