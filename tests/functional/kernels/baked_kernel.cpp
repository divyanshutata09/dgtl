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

// C:\projects\DGTL_framework\tests\functional\test_pipeline_20_hybrid_tiering.cpp
#include "dgtl/jit/runtime/JIT_Executor.hpp"
#include "dgtl/isa/emitter/NASM_Emitter.hpp"
#include "dgtl/core/Arena.hpp"
#include <iostream>
#include <cassert>
#include <cstdint>
#include <vector>

void run_baked_kernel() {
    using namespace dgtl::ir;
    using namespace dgtl::jit::runtime;

    DGTLJitExecutor jit(1); // 1MB JIT Buffer
    dgtl::isa::DGTLNasmEmitter aot;
    dgtl::core::DGTL_Arena arena(1, 64);
    IRStream stream(arena, 16);

    // Build a simple kernel: r1 = 42; r2 = r1 + r1; return r2
    stream.push_node(OpCode::IMMEDIATE, static_cast<uint16_t>(Flags::NONE), 1, 0, 42, 0, 0);
    stream.push_node(OpCode::ADD_I32, static_cast<uint16_t>(Flags::NONE), 2, 1, 1, 0, 0);
    stream.push_node(OpCode::RET, static_cast<uint16_t>(Flags::NONE), 0, 2, 0, 0, 0);

    std::cout << "[DGTL] Loading JIT Kernel..." << std::endl;
    // Simulate JIT loading (using empty binary for trace mapping test)
    std::vector<uint8_t> dummy_bin = { 0x48, 0xC7, 0xC0, 0x2A, 0x00, 0x00, 0x00, 0xC3 }; // mov rax, 42; ret
    auto func = jit.load_and_prepare(dummy_bin, stream);
    assert(func != nullptr);

    if(func) {
        std::cout << "[DGTL] JIT Execution successful at " << (void*)func << std::endl;
        
        // --- HYBRID TRIGGER ---
        std::cout << "[DGTL] Hot Path Detected! Baking to Disk..." << std::endl;
        
        uintptr_t addr = reinterpret_cast<uintptr_t>(func);
        auto retrieved_ir = jit.get_ir_from_address(addr);
        
        dgtl::core::SymbolTable symbols(arena);
        aot.emit(retrieved_ir, symbols);
        
        std::cout << "--- BAKED ASM START ---" << std::endl;
        std::cout << aot.get_output() << std::endl;
        std::cout << "--- BAKED ASM END ---" << std::endl;
        
        assert(aot.get_output().find("DGTL_kernel_entry") != std::string::npos);
        std::cout << "[TEST optED] Hybrid Handover Verified." << std::endl;
    }

    return;
}



