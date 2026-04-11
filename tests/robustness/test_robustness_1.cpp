/* * DGTL-MLIR-X v1.0 : THE COMPILER SPEC
 * TEST SUITE: ROBUSTNESS_01 (NULL-STRIKE)
 * TARGET: BIR 2.1 TRANSACTIONAL INTEGRITY
 */

#include <iostream>
#include <vector>
#include <memory>
#include "dgtl/ir/Global_IR.hpp"
#include "dgtl/pipeline/pass_Manager.hpp"
#include "dgtl/pass/Instruction_Fusion_Synth.hpp" 
#include "dgtl/core/Verified_Formal.hpp"        

using namespace dgtl;

ir::IRNode create_test_node(ir::OpCode op, uint32_t res, uint32_t a, uint32_t b) {
    ir::IRNode node;
    node.opcode = op;
    node.flags = ir::Flags::NONE;
    node.result_id = res;
    node.op_a = a;
    node.op_b = b;
    node.op_c = ir::INVALID_SLOT; 
    node.intent_hash = 0;          
    return node;
}

void run_null_strike_test() {
    std::cout << "--- ROBUSTNESS TEST 1: Null-Strike & Single-Node Safety ---\n";
    
    core::DGTLFormalVerifier formal_verifier;
    ir::IRStream empty_stream;

    std::vector<ir::IRNode> mem;
    mem.push_back(create_test_node(ir::OpCode::ADD_I32, 100, 10, 20));
    ir::IRStream single_node_stream(mem.data(), mem.size());

    opt::DGTLInstructionFusionSynth fusion(formal_verifier);
    pipeline::DGTLoptManager pm(4); 

    try {
        std::cout << "[Step 1] Testing Fusion on Empty Stream... ";
        fusion.synthesize(empty_stream); 
        std::cout << "PASSED (Zero-length safety).\n";

        std::cout << "[Step 2] Testing Fusion on Single Node... ";
        fusion.synthesize(single_node_stream);
        std::cout << "PASSED (Boundary condition safety).\n";

        std::cout << "[Step 3] Testing optManager Parallel Execution... ";
        pm.add_opt(std::make_unique<opt::DGTLInstructionFusionSynth>(formal_verifier));
        pm.execute(empty_stream, pipeline::TargetPrecision::HFT);
        std::cout << "PASSED (Thread-pool safety).\n";

        std::cout << "\nRESULT: TEST 1 SUCCESSFUL. BIR 2.1 Transactional Logic handles null-strikes.\n";
    } catch (const std::exception& e) {
        std::cerr << "FAILED: Exception caught: " << e.what() << "\n";
        throw; // Re-throw to be caught by main
    } catch (...) {
        std::cerr << "FAILED: Unknown error in test logic.\n";
        throw;
    }
} // <--- Function now closed properly

int main() {
    try {
        run_null_strike_test();
    } catch (...) {
        std::cerr << "Fatal: Unhandled exception in test runner." << std::endl;
        return 1;
    }
    return 0;
}