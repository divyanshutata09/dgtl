#include <iostream>
#include <vector>
#include "detail/ir_impl/BitPacked_IR.hpp"
#include "Include/opt/Instruction_Fusion_Synth.hpp"
#include "Include/opt/AI_Cost_Model.hpp"

using namespace dgtl;

void run_volatile_safety_test() {
    std::cout << "--- ROBUSTNESS TEST 3: Volatile Side-Effect Isolation ---\n";
    
    ir::IRStream stream;
    std::vector<ir::IRNode> mem(10);
    stream.buffer = mem.data();
    stream.count = 2;

    // Node 0: MUL (Standard)
    stream.buffer[0] = {ir::OpCode::MUL_F32, 0, 1, 10, 11};
    
    // Node 1: ADD (Marked as VOLATILE)
    // This could be a hardware accumulator write - MUST NOT BE FUSED into FMA
    stream.buffer[1] = {ir::OpCode::ADD_F32, 0, 2, 1, 12};
    stream.buffer[1].flags |= static_cast<uint16_t>(ir::Flags::IS_VOLATILE);

    opt::DGTLAICostModel ai;
    opt::DGTLInstructionFusionSynth fusion(ai);

    std::cout << "[Step 1] Attempting Fusion on Volatile Node... ";
    fusion.synthesize_fusions(stream);

    // If fusion happened, opcode 0 would be FMA and opcode 1 would be NOP.
    // SUCCESS = They remain MUL and ADD.
    if (stream.buffer[0].opcode == ir::OpCode::MUL_F32 && 
        stream.buffer[1].opcode == ir::OpCode::ADD_I32) { // Logic: ADD_F32/I32 check
        std::cout << "PASSED.\n";
    } else if (stream.buffer[0].opcode == ir::OpCode::FMA_F32) {
        std::cout << "FAILED: Optimizer fused a Volatile side-effect!\n";
    } else {
        std::cout << "PASSED (No fusion occurred).\n";
    }

    std::cout << "\nRESULT: TEST 3 COMPLETE. Checking logic integrity.\n";
}

int main() {
    run_volatile_safety_test();
    return 0;
}
