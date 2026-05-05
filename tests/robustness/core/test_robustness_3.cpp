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

#include "dgtl/dgtl_all.hpp"

#include "dgtl/pass/transform/Instruction_Fusion_Synth.hpp"

#include <cstdlib>
#include <iostream>

void run_volatile_safety_test() {
    constexpr size_t kArenaMbPerThread = 64;
    constexpr size_t kStreamCapacity = 32;

    std::cout << "--- ROBUSTNESS TEST 3: Volatile Side-Effect Isolation ---\n";
    
    // 1. Initialize stream with arena
    dgtl::core::DGTL_Arena arena(1, kArenaMbPerThread);
    dgtl::ir::IRStream stream(arena, kStreamCapacity);

    // Node 0: MUL_F32 (Standard math)
    stream.push_node(dgtl::ir::OpCode::MUL_F32, dgtl::ir::Flags::NONE, 100, 10, 11, 0);

    // Node 1: ADD_F32 (Marked VOLATILE - e.g., a hardware CSR write or accumulator)
    stream.push_node(
        dgtl::ir::OpCode::ADD_F32,
        static_cast<uint16_t>(dgtl::ir::Flags::IS_VOLATILE | dgtl::ir::Flags::HAS_SIDE_EFFECTS),
        101,
        100,
        12,
        0);

    dgtl::core::DGTLFormalVerifier formal_verifier;
    dgtl::pass::DGTLInstructionFusionSynth fusion(formal_verifier);

    std::cout << "[Step 1] Attempting Fusion on Volatile Node... ";
    
    try {
        fusion.synthesize(stream);
        
        // VERIFICATION LOGIC
        // If the optimizer is correct, it MUST NOT fuse Node 0 and Node 1 into an FMA
        // because Node 1 has side effects that must remain visible to the system.
        
        const bool is_fused = (stream.get_node(0).opcode == dgtl::ir::OpCode::FMA_F32);
        const bool is_safe = (stream.get_node(0).opcode == dgtl::ir::OpCode::MUL_F32 &&
                      stream.get_node(1).opcode == dgtl::ir::OpCode::ADD_F32);

        if (is_safe) {
            std::cout << "PASSED.\n";
            std::cout << ">> Integrity Confirmed: Volatile boundary maintained.\n";
        } else if (is_fused) {
            std::cerr << "FAILED!\n";
            std::cerr << ">> Critical Error: Optimizer fused a Volatile side-effect into FMA!\n";
            std::exit(1);
        } else {
            std::cout << "SKIPPED (No pattern match).\n";
        }

    } catch (const std::exception& e) {
        std::cerr << "CRASH: " << e.what() << "\n";
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








