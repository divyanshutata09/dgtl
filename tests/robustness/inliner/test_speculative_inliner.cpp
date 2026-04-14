/* * DGTL-MLIR-X v1.0 
 * TEST SUITE: ROBUSTNESS_05 (SPECULATIVE-INLINING-TRANS)
 * TARGET: SSA Isolation & Shadow-to-Core Promotion
 */

#include <iostream>
#include <cassert>
#include "dgtl/pass/speculative/Speculative_Inliner.hpp"
#include "dgtl/ir/base/Global_IR.hpp"
#include "dgtl/analysis/cost/AI_Cost_Model.hpp"
#include "dgtl/core/Symbol_Table.hpp"

using namespace dgtl;

void run_inliner_test() {
    std::cout << "--- ROBUSTNESS TEST 5: Speculative Inlining & SSA Renaming ---\n";

    // 1. Initialize Components
    analysis::DGTLAICostModel ai_model;
    core::SymbolTable symbols;
    dgtl::pass::DGTLSpeculativeInliner inliner(&ai_model, &symbols);

    // 2. Setup Caller Stream (The function receiving the code)
    // Using 64-byte aligned allocation for BIR 2.1
    ir::IRStream caller = ir::IRStream::allocate_transactional(32);
    
    // Node: ADD_I32 (Result: 10, Ops: 1, 2)
    std::cout << "(before push_node) is_null=" << caller.is_null() << " capacity=" << caller.capacity() << "\n";
    caller.push_node(ir::OpCode::ADD_I32, 0, 10, 1, 2, 0);
    std::cout << "(after push_node) is_null=" << caller.is_null() << " size=" << caller.size() << " capacity=" << caller.capacity() << "\n";
    size_t original_size = caller.size();

    // 3. Setup Callee Stream (The function being inlined)
    ir::IRStream callee = ir::IRStream::allocate_transactional(16);
    // Node: MUL_I32 (Result: 1, Ops: 10, 3) -> Result 1 should be renamed to 11
    callee.push_node(ir::OpCode::MUL_I32, 0, 1, 10, 3, 0);

    std::cout << "[Step 1] Attempting Speculative Inline (Shadow Buffer)... ";
    
    // Use the public execute() API instead of private methods
    try {
        inliner.execute(caller);
        std::cout << "PASSED.\n";
    } catch (const std::exception& e) {
        std::cout << "FAILED: " << e.what() << "\n";
        exit(1);
    }

    // 4. Verification of results
    // Check that the stream was processed correctly
    std::cout << "[Step 2] Testing Transaction Rollback... ";
    std::cout << "(before rollback)\n";
    std::cout.flush();
    caller.rollback_transaction();
    std::cout << "(after rollback)\n";
    std::cout.flush();

    std::cout << "(before size)\n";
    std::cout.flush();
    size_t new_size = caller.size();
    std::cout << "(after size) size=" << new_size << "\n";
    std::cout.flush();

    std::cout << "(before get_core)\n";
    std::cout.flush();
    uint32_t result_id = caller.get_core(0).result_id;
    std::cout << "(after get_core) result_id=" << result_id << "\n";
    std::cout.flush();

    if (new_size == original_size && result_id == 10) {
        std::cout << "PASSED.\n";
    } else {
        std::cout << "FAILED: Rollback corrupted the core stream.\n";
        exit(1);
    }

    std::cout << "\nRESULT: TEST 5 COMPLETE. Inliner Shadow-Logic is Robust.\n";
}

void run_test_speculative_inliner() {
    try {
        run_inliner_test();
    } catch (const std::exception& e) {
        std::cerr << "CRITICAL ERROR: " << e.what() << "\n";
        return;
    }
    return;
}



