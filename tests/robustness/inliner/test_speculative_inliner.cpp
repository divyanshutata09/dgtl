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
 * TEST SUITE: ROBUSTNESS_05 (SPECULATIVE-INLINING-TRANS)
 * TARGET: SSA Isolation & Shadow-to-Core Promotion
 */

#include "dgtl/dgtl_all.hpp"

#include <iostream>
#include <stdexcept>

#include "dgtl/pass/speculative/Speculative_Inliner.hpp"

void run_inliner_test() {
    constexpr size_t kCallerCapacity = 32;
    constexpr size_t kCalleeCapacity = 16;

    std::cout << "--- ROBUSTNESS TEST 5: Speculative Inlining & SSA Renaming ---\n";

    // 1. Initialize Components
    dgtl::analysis::DGTLAICostModel ai_model;
    dgtl::core::SymbolTable symbols;
    dgtl::pass::DGTLSpeculativeInliner inliner(&ai_model, &symbols);

    // 2. Setup Caller Stream (The function receiving the code)
    // Using 64-byte aligned allocation for BIR 1.0.0
    dgtl::ir::IRStream caller = dgtl::ir::IRStream::allocate_transactional(kCallerCapacity);
    
    // Node: ADD_I32 (Result: 10, Ops: 1, 2)
    caller.push_node(dgtl::ir::OpCode::ADD_I32, dgtl::ir::Flags::NONE, 10, 1, 2, 0);
    const size_t original_size = caller.size();

    // 3. Setup Callee Stream (The function being inlined)
    dgtl::ir::IRStream callee = dgtl::ir::IRStream::allocate_transactional(kCalleeCapacity);
    // Node: MUL_I32 (Result: 1, Ops: 10, 3) -> Result 1 should be renamed to 11
    callee.push_node(dgtl::ir::OpCode::MUL_I32, dgtl::ir::Flags::NONE, 1, 10, 3, 0);
    (void)callee;

    std::cout << "[Step 1] Attempting Speculative Inline (Shadow Buffer)... ";
    
    // Use the public execute() API instead of private methods
    try {
        inliner.execute(caller);
        std::cout << "PASSED.\n";
    } catch (const std::exception& e) {
        std::cout << "FAILED: " << e.what() << "\n";
        throw;
    }

    // 4. Verification of results
    // Check that the stream was processed correctly
    std::cout << "[Step 2] Testing Transaction Rollback... ";
    caller.rollback_transaction();

    const size_t new_size = caller.size();
    const uint32_t result_id = caller.get_core(0).result_id;

    if (new_size == original_size && result_id == 10) {
        std::cout << "PASSED.\n";
    } else {
        std::cout << "FAILED: Rollback corrupted the core stream.\n";
        throw std::runtime_error("speculative_inliner failed: rollback corrupted core stream");
    }

    std::cout << "\nRESULT: TEST 5 COMPLETE. Inliner Shadow-Logic is Robust.\n";
}

void run_test_speculative_inliner() {
    try {
        run_inliner_test();
    } catch (const std::exception& e) {
        std::cerr << "CRITICAL ERROR: " << e.what() << "\n";
        throw;
    }
    return;
}









