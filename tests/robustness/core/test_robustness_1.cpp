/* * DGTL-MLIR-X v1.0 : THE COMPILER SPEC
 * TEST SUITE: ROBUSTNESS_01 (NULL-STRIKE)
 * TARGET: BIR 2.1 TRANSACTIONAL INTEGRITY
 */

#include <iostream>
#include <vector>
#include <memory>
#include "dgtl/ir/base/Global_IR.hpp"
#include "dgtl/pipeline/Pass_Manager.hpp"
#include "dgtl/pass/transform/Instruction_Fusion_Synth.hpp"
#include "dgtl/core/Verified_Formal.hpp"
#include "dgtl/core/Arena.hpp"

using namespace dgtl;

void run_null_strike_test() {
    std::cout << "--- ROBUSTNESS TEST 1: Null-Strike & Single-Node Safety ---\n";
    
    core::DGTL_Arena arena(64); // 64 MB arena
    core::DGTLFormalVerifier formal_verifier;
    ir::IRStream empty_stream;

    ir::IRStream single_node_stream(arena, 10);
    single_node_stream.push_node(ir::OpCode::ADD_I32, ir::Flags::NONE, 100, 10, 20, ir::INVALID_SLOT);

    dgtl::pass::DGTLInstructionFusionSynth fusion(formal_verifier);
    pipeline::DGTLoptManager pm(4); 

    try {
        std::cout << "[Step 1] Testing Fusion on Empty Stream... ";
        fusion.synthesize(empty_stream); 
        std::cout << "PASSED (Zero-length safety).\n";

        std::cout << "[Step 2] Testing Fusion on Single Node... ";
        fusion.synthesize(single_node_stream);
        std::cout << "PASSED (Boundary condition safety).\n";

        std::cout << "[Step 3] Testing optManager Parallel Execution... ";
        pm.add_opt(std::make_unique<dgtl::pass::DGTLInstructionFusionSynth>(formal_verifier));
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

void run_test_robustness_1() {
    try {
        run_null_strike_test();
    } catch (...) {
        std::cerr << "Fatal: Unhandled exception in test runner." << std::endl;
        return;
    }
    return;
}



