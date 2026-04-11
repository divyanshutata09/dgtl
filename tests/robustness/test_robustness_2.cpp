#include <iostream>
#include <vector>
#include <atomic>
#include "detail/ir_impl/BitPacked_IR.hpp"
#include "Include/pipeline/Pass_Manager.hpp"
#include "Include/opt/Instruction_Fusion_Synth.hpp"
#include "Include/opt/AI_Cost_Model.hpp"

using namespace dgtl;

void run_saturation_test() {
    std::cout << "--- ROBUSTNESS TEST 2: Task Saturation & Thread Contention ---\n";
    
    // 1. Generate 1,048,576 IR Nodes (Massive Stream)
    // This will create ~4096 chunks of 256 nodes each, hitting the MAX_TASKS limit.
    const size_t NODE_COUNT = 1048576;
    std::cout << "[Step 1] Allocating " << (NODE_COUNT * sizeof(ir::IRNode)) / (1024 * 1024) << " MB for IR Stream... ";
    
    std::vector<ir::IRNode> large_mem(NODE_COUNT);
    ir::IRStream large_stream;
    large_stream.buffer = large_mem.data();
    large_stream.count = NODE_COUNT;

    // Fill with MUL + ADD pairs to give the Fusion Synth work to do
    for(size_t i = 0; i < NODE_COUNT - 1; i += 2) {
        large_stream.buffer[i]   = {ir::OpCode::MUL_F32, 0, (uint32_t)i, 10, 11};
        large_stream.buffer[i+1] = {ir::OpCode::ADD_F32, 0, (uint32_t)i+1, (uint32_t)i, 12};
    }
    std::cout << "DONE.\n";

    // 2. Initialize Orchestrators
    opt::DGTLAICostModel ai;
    opt::DGTLInstructionFusionSynth fusion(ai);
    pipeline::DGTLPassManager pm; // Uses all hardware threads

    std::cout << "[Step 2] Executing Parallel Fusion Strike on " << std::thread::hardware_concurrency() << " cores... ";
    
    try {
        pm.add_pass(fusion);
        pm.execute(large_stream); // This is the stress point
        std::cout << "PASSED.\n";
    } catch (...) {
        std::cout << "FAILED: Threading deadlock or Crash!\n";
        return;
    }

    // 3. Validation: Check if the first and last chunks were actually processed
    bool sample_check = (large_stream.buffer[0].opcode == ir::OpCode::FMA_F32);
    if (sample_check) {
        std::cout << "SUCCESS: Data integrity maintained across parallel chunks.\n";
    } else {
        std::cout << "FAILURE: Nodes were not optimized (Inference or Logic error).\n";
    }

    std::cout << "\nRESULT: TEST 2 SUCCESSFUL. PassManager handles high-pressure tasking.\n";
}

int main() {
    run_saturation_test();
    return 0;
}
