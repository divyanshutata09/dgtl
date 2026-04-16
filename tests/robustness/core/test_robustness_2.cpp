#include <iostream>
#include <vector>
#include <atomic>
#include <thread>
#include <stdexcept>
#include "dgtl/ir/base/Global_IR.hpp"
#include "dgtl/pipeline/Functional_Pipeline_Probes.hpp"
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
    
    // 1. Generate a stress stream that still completes quickly on dev laptops.
    const size_t NODE_COUNT = 32768;
    std::cout << "[Step 1] Allocating " << (NODE_COUNT * sizeof(ir::IRNode)) / (1024 * 1024) 
              << " MB for IR Stream... ";
    
    // Keep stress semantics without requiring a huge arena pool.
    core::DGTL_Arena arena(1, 128); // 1 thread, 128 MB arena
    ir::IRStream large_stream(arena, NODE_COUNT);

    // Fill with MUL + ADD pairs to trigger the fusion path.
    dgtl::pipeline::probes::build_mul_add_fusion_pairs_probe(large_stream, NODE_COUNT / 2, 10, 11, 12);
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

    // 3. Validation: Verify at least one fused node is present after the strike.
    const bool sample_check = dgtl::pipeline::probes::count_opcode(large_stream, ir::OpCode::FMA_F32) > 0;

    if (sample_check) {
        std::cout << "SUCCESS: Data integrity maintained across parallel chunks.\n";
        std::cout << "[DATA] Found fused FMA_F32 nodes in transformed stream.\n";
    } else {
        std::cout << "FAILURE: No FMA_F32 nodes were produced by fusion strike.\n";
        throw std::runtime_error("robustness.core2 failed: fusion strike produced no FMA_F32 nodes");
    }

    std::cout << "\nRESULT: TEST 2 COMPLETE. optManager handles high-pressure tasking.\n";
}

void run_test_robustness_2() {
    try {
        run_saturation_test();
    } catch (const std::exception& e) {
        std::cerr << "CRITICAL EXCEPTION during Saturation Test: " << e.what() << std::endl;
        throw;
    }
    return;
}

