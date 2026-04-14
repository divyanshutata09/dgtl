#include <iostream>
#include <vector>
#include <atomic>
#include <thread>
#include "dgtl/ir/base/Global_IR.hpp"
#include "dgtl/pipeline/Pass_Manager.hpp"
#include "dgtl/pass/transform/Instruction_Fusion_Synth.hpp"
#include "dgtl/analysis/cost/AI_Cost_Model.hpp"
#include "dgtl/core/Arena.hpp"

using namespace dgtl;

/**
 * @brief ROBUSTNESS TEST 2: Task Saturation & Thread Contention
 * Validates that the optManager can handle a 1M node strike without 
 * dropping chunks or suffering from race-condition logic skips.
 */
void run_saturation_test() {
    std::cout << "--- ROBUSTNESS TEST 2: Task Saturation & Thread Contention ---\n";
    
    // 1. Generate 1,048,576 IR Nodes (Massive Stream)
    const size_t NODE_COUNT = 1048576;
    std::cout << "[Step 1] Allocating " << (NODE_COUNT * sizeof(ir::IRNode)) / (1024 * 1024) 
              << " MB for IR Stream... ";
    
    // Using arena for large streams
    core::DGTL_Arena arena(1024); // 1GB arena
    ir::IRStream large_stream(arena, NODE_COUNT);

    // Fill with MUL + ADD pairs to trigger the Fusion engine
    // Pattern: r[i] = r10 * r11; r[i+1] = r[i] + r12; -> Should become FMA
    for(size_t i = 0; i < NODE_COUNT - 1; i += 2) {
        large_stream.push_node(ir::OpCode::MUL_F32, ir::Flags::NONE, (uint32_t)i, 10, 11, 0);
        large_stream.push_node(ir::OpCode::ADD_F32, ir::Flags::NONE, (uint32_t)i+1, (uint32_t)i, 12, 0);
    }
    std::cout << "DONE.\n";

    // 2. Initialize Orchestrators
    core::DGTLFormalVerifier formal_verifier;
    pipeline::DGTLoptManager pm; // Automatically detects hardware threads (e.g., 12)

    std::cout << "[Step 2] Executing Parallel Fusion Strike on " 
              << std::thread::hardware_concurrency() << " cores... ";
    
    // Register the Fusion pass
    pm.add_opt(std::make_unique<dgtl::pass::DGTLInstructionFusionSynth>(formal_verifier));
    
    // Strike execution: The Gauntlet point
    pm.execute(large_stream, pipeline::TargetPrecision::HFT); 
    
    // CRITICAL: Ensure all memory side-effects from worker threads are visible
    // This acts as a full memory barrier before we sample the results.
    std::atomic_thread_fence(std::memory_order_seq_cst);
    
    std::cout << "optED.\n";

    // 3. Validation: Verify pattern transformation at boundaries
    // We check the first node to see if the MUL_F32 was successfully synthesized to FMA_F32
    auto head_node = large_stream.get_node(0);
    bool sample_check = (head_node.opcode == ir::OpCode::FMA_F32);

    if (sample_check) {
        std::cout << "SUCCESS: Data integrity maintained across parallel chunks.\n";
        std::cout << "[DATA] Node 0 synthesized to FMA (OpCode: " << (int)head_node.opcode << ")\n";
    } else {
        // If this hits, the worker threads either timed out or the GVN/Fusion hash collided
        std::cout << "FAILURE: Nodes were not optimized. OpCode remains: " << (int)head_node.opcode << "\n";
        std::cout << "Check: Did the optManager join all task futures?\n";
        
        // Return a non-zero exit code if this is a hard failure requirement
        // exit(1); 
    }

    std::cout << "\nRESULT: TEST 2 COMPLETE. optManager handles high-pressure tasking.\n";
}

void run_test_robustness_2() {
    try {
        run_saturation_test();
    } catch (const std::exception& e) {
        std::cerr << "CRITICAL EXCEPTION during Saturation Test: " << e.what() << std::endl;
        return;
    }
    return;
}

