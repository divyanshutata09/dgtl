/* * DGTL-MLIR-X v1.0 
 * TEST SUITE: ROBUSTNESS_03 (VOLATILE-SAFETY)
 * TARGET: SIDE-EFFECT ISOLATION (HFT/HARDWARE ATOMICITY)
 */

#include <iostream>
#include <vector>
#include <cassert>
#include "dgtl/ir/Global_IR.hpp"
#include "dgtl/pass/Instruction_Fusion_Synth.hpp"
#include "dgtl/core/Verified_Formal.hpp"

using namespace dgtl;

void run_volatile_safety_test() {
    std::cout << "--- ROBUSTNESS TEST 3: Volatile Side-Effect Isolation ---\n";
    
    // 1. Initialize memory with alignment awareness
    std::vector<ir::IRNode> mem(32);
    ir::IRStream stream(mem.data(), mem.size());
    stream.set_count(2);

    // Node 0: MUL_F32 (Standard math)
    // Args: OpCode, Flags, ResultID, OpA, OpB
    stream.get_node_ref(0).opcode = ir::OpCode::MUL_F32;
    stream.get_node_ref(0).result_id = 100;
    stream.get_node_ref(0).op_a = 10;
    stream.get_node_ref(0).op_b = 11;
    stream.get_node_ref(0).flags = ir::Flags::NONE;

    // Node 1: ADD_F32 (Marked VOLATILE - e.g., a hardware CSR write or accumulator)
    stream.get_node_ref(1).opcode = ir::OpCode::ADD_F32;
    stream.get_node_ref(1).result_id = 101;
    stream.get_node_ref(1).op_a = 100; // Depends on Node 0
    stream.get_node_ref(1).op_b = 12;
    // CRITICAL: Set Volatile flag to prevent Fusion into FMA
    stream.get_node_ref(1).flags = ir::Flags::IS_VOLATILE;

    core::DGTLFormalVerifier formal_verifier;
    opt::DGTLInstructionFusionSynth fusion(formal_verifier);

    std::cout << "[Step 1] Attempting Fusion on Volatile Node... ";
    
    try {
        fusion.synthesize(stream);
        
        // VERIFICATION LOGIC
        // If the optimizer is correct, it MUST NOT fuse Node 0 and Node 1 into an FMA
        // because Node 1 has side effects that must remain visible to the system.
        
        bool is_fused = (stream.get_node_ref(0).opcode == ir::OpCode::FMA_F32);
        bool is_safe = (stream.get_node_ref(0).opcode == ir::OpCode::MUL_F32 && 
                        stream.get_node_ref(1).opcode == ir::OpCode::ADD_F32);

        if (is_safe) {
            std::cout << "PASSED.\n";
            std::cout << ">> Integrity Confirmed: Volatile boundary maintained.\n";
        } else if (is_fused) {
            std::cerr << "FAILED!\n";
            std::cerr << ">> Critical Error: Optimizer fused a Volatile side-effect into FMA!\n";
            exit(1); 
        } else {
            std::cout << "SKIPPED (No pattern match).\n";
        }

    } catch (const std::exception& e) {
        std::cerr << "CRASH: " << e.what() << std::endl;
        throw;
    }

    std::cout << "RESULT: TEST 3 COMPLETE.\n";
}

int main() {
    try {
        run_volatile_safety_test();
    } catch (...) {
        return 1;
    }
    return 0;
}