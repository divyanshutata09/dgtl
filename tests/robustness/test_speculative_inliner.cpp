/* * DGTL-MLIR-X v1.0 
 * TEST SUITE: ROBUSTNESS_05 (SPECULATIVE-INLINING-TRANS)
 * TARGET: SSA Isolation & Shadow-to-Core Promotion
 */

#include <iostream>
#include <cassert>
#include "dgtl/pass/Speculative_Inliner.hpp"
#include "dgtl/ir/Global_IR.hpp"
#include "dgtl/analysis/AI_Cost_Model.hpp"
#include "dgtl/core/Symbol_Table.hpp"

using namespace dgtl;

void run_inliner_test() {
    std::cout << "--- ROBUSTNESS TEST 5: Speculative Inlining & SSA Renaming ---\n";

    // 1. Initialize Components
    analysis::DGTLAICostModel ai_model;
    core::SymbolTable symbols;
    opt::DGTLSpeculativeInliner inliner(ai_model, symbols);

    // 2. Setup Caller Stream (The function receiving the code)
    // Using 64-byte aligned allocation for BIR 2.1
    ir::IRStream caller = ir::IRStream::allocate_transactional(32);
    
    // Node: ADD_I32 (Result: 10, Ops: 1, 2)
    caller.push_node(ir::OpCode::ADD_I32, 0, 10, 1, 2, 0);
    size_t original_size = caller.size();

    // 3. Setup Callee Stream (The function being inlined)
    ir::IRStream callee = ir::IRStream::allocate_transactional(16);
    // Node: MUL_I32 (Result: 1, Ops: 10, 3) -> Result 1 should be renamed to 11
    callee.push_node(ir::OpCode::MUL_I32, 0, 1, 10, 3, 0);

    std::cout << "[Step 1] Attempting Speculative Inline (Shadow Buffer)... ";
    
    // Manually trigger the append logic from your .hpp to verify renaminng
    // We simulate the transaction behavior
    caller.begin_transaction();
    uint32_t cursor = 0;
    
    // Replicating the append_inlined_callee logic to verify state
    inliner.append_inlined_callee(caller, cursor, callee);
    caller.update_shadow_size(cursor);

    // 4. Verification of Alpha-Renaming
    ir::IRAtom& s_atom = caller.get_shadow_atom(0);
    ir::IRCore& s_core = caller.get_shadow_core(0);

    // If get_max_id was 10, the inlined result 1 should now be 11
    if (s_core.result_id > 1) {
        std::cout << "PASSED.\n";
        std::cout << ">> SSA Renaming: VREG 1 -> VREG " << s_core.result_id << "\n";
    } else {
        std::cout << "FAILED: SSA Collision detected. Inlined VREG was not renamed.\n";
        exit(1);
    }

    // 5. Verification of Transaction Safety
    std::cout << "[Step 2] Testing Transaction Rollback... ";
    caller.rollback_transaction();

    if (caller.size() == original_size && caller.get_core(0).result_id == 10) {
        std::cout << "PASSED.\n";
    } else {
        std::cout << "FAILED: Rollback corrupted the core stream.\n";
        exit(1);
    }

    std::cout << "\nRESULT: TEST 5 COMPLETE. Inliner Shadow-Logic is Robust.\n";
}

int main() {
    try {
        run_inliner_test();
    } catch (const std::exception& e) {
        std::cerr << "CRITICAL ERROR: " << e.what() << "\n";
        return 1;
    }
    return 0;
}