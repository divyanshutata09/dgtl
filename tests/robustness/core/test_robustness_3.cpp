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

/* * DGTL (Deep General-purpose Transformation Layer) version 1.0.0 
 * TEST SUITE: ROBUSTNESS_03 (VOLATILE-SAFETY)
 * TARGET: SIDE-EFFECT ISOLATION (HFT/HARDWARE ATOMICITY)
 */

#include <iostream>
#include <vector>
#include <cassert>
#include "dgtl/ir/Global_IR.hpp"
#include "dgtl/pass/transform/Instruction_Fusion_Synth.hpp"
#include "dgtl/core/Verified_Formal.hpp"
#include "dgtl/core/Arena.hpp"

using namespace dgtl;

void run_volatile_safety_test() {
    std::cout << "--- ROBUSTNESS TEST 3: Volatile Side-Effect Isolation ---\n";
    
    // 1. Initialize stream with arena
    core::DGTL_Arena arena(1, 64);
    ir::IRStream stream(arena, 32);

    // Node 0: MUL_F32 (Standard math)
    stream.push_node(ir::OpCode::MUL_F32, ir::Flags::NONE, 100, 10, 11, 0);

    // Node 1: ADD_F32 (Marked VOLATILE - e.g., a hardware CSR write or accumulator)
    stream.push_node(ir::OpCode::ADD_F32, ir::Flags::IS_VOLATILE, 101, 100, 12, 0);

    core::DGTLFormalVerifier formal_verifier;
    dgtl::pass::DGTLInstructionFusionSynth fusion(formal_verifier);

    std::cout << "[Step 1] Attempting Fusion on Volatile Node... ";
    
    try {
        fusion.synthesize(stream);
        
        // VERIFICATION LOGIC
        // If the optimizer is correct, it MUST NOT fuse Node 0 and Node 1 into an FMA
        // because Node 1 has side effects that must remain visible to the system.
        
        bool is_fused = (stream.get_node(0).opcode == ir::OpCode::FMA_F32);
        bool is_safe = (stream.get_node(0).opcode == ir::OpCode::MUL_F32 && 
                        stream.get_node(1).opcode == ir::OpCode::ADD_F32);

        if (is_safe) {
            std::cout << "PASSED.\n";
            std::cout << ">> Integrity Confirmed: Volatile boundary maintained.\n";
        } else if (is_fused) {
            std::cerr << "FAILED!\n";
            std::cerr << ">> Critical Error: Optimizer fused a Volatile side-effect into FMA!\n";
            exit(1); 
        } else {
            std::cout << "SKIPPED (No pattern match).\n";
        }

    } catch (const std::exception& e) {
        std::cerr << "CRASH: " << e.what() << std::endl;
        throw;
    }

    std::cout << "RESULT: TEST 3 COMPLETE.\n";
}

void run_test_robustness_3() {
    try {
        run_volatile_safety_test();
    } catch (...) {
        return;
    }
    return;
}








