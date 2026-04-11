#include <iostream>
#include <vector>
#include "detail/ir_impl/BitPacked_IR.hpp"
#include "Include/pipeline/Pass_Manager.hpp"
#include "Include/opt/Instruction_Fusion_Synth.hpp"
#include "Include/opt/AI_Cost_Model.hpp"

using namespace dgtl;

void run_null_strike_test() {
    std::cout << "--- ROBUSTNESS TEST 1: Null-Strike & Single-Node Safety ---\n";
    
    // 1. Setup an empty stream
    ir::IRStream empty_stream;
    empty_stream.buffer = nullptr;
    empty_stream.count = 0;

    // 2. Setup a single-node stream (Fusion requires 2+ nodes)
    ir::IRStream single_node_stream;
    std::vector<ir::IRNode> mem(1);
    mem[0] = {ir::OpCode::ADD_I32, 0, 1, 10, 20};
    single_node_stream.buffer = mem.data();
    single_node_stream.count = 1;

    // 3. Initialize Orchestrators
    opt::DGTLAICostModel ai;
    opt::DGTLInstructionFusionSynth fusion(ai);
    pipeline::DGTLPassManager pm(4); // 4 threads

    try {
        std::cout << "[Step 1] Testing Fusion on Empty Stream... ";
        fusion.synthesize_fusions(empty_stream); 
        std::cout << "PASSED.\n";

        std::cout << "[Step 2] Testing Fusion on Single Node... ";
        fusion.synthesize_fusions(single_node_stream);
        std::cout << "PASSED.\n";

        std::cout << "[Step 3] Testing PassManager Execution on Empty Stream... ";
        pm.add_pass(fusion);
        pm.execute(empty_stream);
        std::cout << "PASSED.\n";

    } catch (...) {
        std::cout << "FAILED: Exception or Crash detected!\n";
        return;
    }

    std::cout << "\nRESULT: TEST 1 SUCCESSFUL. Logic handles boundary conditions.\n";
}

int main() {
    run_null_strike_test();
    return 0;
}
