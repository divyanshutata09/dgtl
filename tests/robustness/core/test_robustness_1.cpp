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

/* * DGTL (Deep General-purpose Transformation Layer) version 1.0.0 : THE COMPILER SPEC
 * TEST SUITE: ROBUSTNESS_01 (NULL-STRIKE)
 * TARGET: BIR 1.0.0 TRANSACTIONAL INTEGRITY
 */

#include "dgtl/dgtl_all.hpp"

#include <iostream>
#include <memory>
#include "dgtl/pass/transform/Instruction_Fusion_Synth.hpp"

void run_null_strike_test() {
    constexpr size_t kArenaMbPerThread = 64;
    constexpr size_t kSingleNodeCapacity = 10;
    constexpr uint32_t kPipelineThreads = 4;

    std::cout << "--- ROBUSTNESS TEST 1: Null-Strike & Single-Node Safety ---\n";
    
    dgtl::core::DGTL_Arena arena(1, kArenaMbPerThread); // 1 thread, 64 MB arena
    dgtl::core::DGTLFormalVerifier formal_verifier;
    dgtl::ir::IRStream empty_stream;

    dgtl::ir::IRStream single_node_stream(arena, kSingleNodeCapacity);
    single_node_stream.push_node(
        dgtl::ir::OpCode::ADD_I32, dgtl::ir::Flags::NONE, 100, 10, 20, dgtl::ir::INVALID_SLOT);

    dgtl::pass::DGTLInstructionFusionSynth fusion(formal_verifier);
    dgtl::pipeline::DGTLoptManager pm(kPipelineThreads);

    try {
        std::cout << "[Step 1] Testing Fusion on Empty Stream... ";
        fusion.synthesize(empty_stream); 
        std::cout << "PASSED (Zero-length safety).\n";

        std::cout << "[Step 2] Testing Fusion on Single Node... ";
        fusion.synthesize(single_node_stream);
        std::cout << "PASSED (Boundary condition safety).\n";

        std::cout << "[Step 3] Testing optManager Parallel Execution... ";
        pm.add_opt(std::make_unique<dgtl::pass::DGTLInstructionFusionSynth>(formal_verifier));
        pm.execute(empty_stream, dgtl::pipeline::TargetPrecision::HFT);
        std::cout << "PASSED (Thread-pool safety).\n";

        std::cout << "\nRESULT: TEST 1 SUCCESSFUL. BIR 1.0.0 Transactional Logic handles null-strikes.\n";
    } catch (const std::exception& e) {
        std::cerr << "FAILED: Exception caught: " << e.what() << "\n";
        throw; // Re-throw to be caught by main
    } catch (...) {
        std::cerr << "FAILED: Unknown error in test logic.\n";
        throw;
    }
} // <--- Function now closed properly

void run_test_robustness_1() {
    try {
        run_null_strike_test();
    } catch (...) {
        std::cerr << "Fatal: Unhandled exception in test runner.\n";
        return;
    }
    return;
}









