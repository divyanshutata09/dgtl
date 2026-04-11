#include <iostream>
#include <vector>
#include <cassert>

// Framework Includes
#include "dgtl/ir/Global_IR.hpp"
#include "dgtl/pass/GVN.hpp"
#include "dgtl/pass/Instruction_Folder.hpp"
#include "dgtl/pass/Instruction_Fusion_Synth.hpp"
#include "dgtl/core/Verified_Formal.hpp" // Required for Folder
#include "dgtl/analysis/AI_Cost_Model.hpp"   // Required for Fusion

using namespace dgtl;

/**
 * @brief TEST 1: Verifies that GVN handles deep dependency chains.
 */
void run_dependency_chain_test() {
    std::cout << "\n--- TEST 1: Dependency Chain & GVN Resolution ---\n";
    ir::IRStream stream;
    std::vector<ir::IRNode> mem(100);
    stream.buffer = mem.data();
    stream.count = 0;

    // Node 0: r1 = 10 + 11
    stream.buffer[stream.count++] = {ir::OpCode::ADD_I32, 0, 1, 10, 11};
    // Node 1: r2 = 10 + 11 (Redundant)
    stream.buffer[stream.count++] = {ir::OpCode::ADD_I32, 0, 2, 10, 11};
    // Node 2: r3 = r2 + 5  (Should resolve r2 -> r1, becoming r1 + 5)
    stream.buffer[stream.count++] = {ir::OpCode::ADD_I32, 0, 3, 2, 5};

    opt::DGTLGVN gvn;
    gvn.run(stream);

    std::cout << "Target Node OpA (Result ID): " << stream.buffer[2].op_a << "\n";
    
    if (stream.buffer[2].op_a == 1 && stream.buffer[1].opcode == ir::OpCode::NOP) {
        std::cout << "SUCCESS: Dependency chain compressed. r2 eliminated.\n";
    } else {
        std::cout << "FAILURE: GVN did not resolve the duplicate node.\n";
    }
}

/**
 * @brief TEST 2: Verifies SMT-based Constant Folding.
 */
void run_folding_test() {
    std::cout << "\n--- TEST 2: Constant Instruction Folding ---\n";
    ir::IRStream stream;
    std::vector<ir::IRNode> mem(100);
    stream.buffer = mem.data();
    stream.count = 0;

    // Node 0: r1 = 100 + 200 (Marked as Speculative Constants)
    ir::IRNode fold_node = {ir::OpCode::ADD_I32, 0, 1, 100, 200};
    fold_node.flags |= static_cast<uint16_t>(ir::Flags::IS_SPEC_CONST);
    stream.buffer[stream.count++] = fold_node;

    opt::DGTLFormalVerifier verifier;
    opt::DGTLInstructionFolder folder(verifier);
    folder.fold_stream(stream);

    if (stream.buffer[0].opcode == ir::OpCode::LOAD) {
        std::cout << "SUCCESS: 100+200 folded into LOAD immediate.\n";
        std::cout << "Folded Result: " << stream.buffer[0].op_a << "\n";
    } else {
        std::cout << "FAILURE: Instruction folder skipped the node.\n";
    }
}

/**
 * @brief TEST 3: Verifies Macro-Op Fusion (MUL + ADD -> FMA).
 */
void run_fusion_test() {
    std::cout << "\n--- TEST 3: Macro-Op Fusion (FMA) ---\n";
    ir::IRStream stream;
    std::vector<ir::IRNode> mem(100);
    stream.buffer = mem.data();
    stream.count = 0;

    // Node 0: MUL r1 = r10 * r11
    stream.buffer[stream.count++] = {ir::OpCode::MUL_F32, 0, 1, 10, 11};
    // Node 1: ADD r2 = r1 + r12 (Uses MUL result)
    stream.buffer[stream.count++] = {ir::OpCode::ADD_F32, 0, 2, 1, 12};

    opt::DGTLAICostModel ai;
    opt::DGTLInstructionFusionSynth fusion(ai);
    fusion.synthesize_fusions(stream);

    if (stream.buffer[0].opcode == ir::OpCode::FMA_F32 && stream.buffer[1].opcode == ir::OpCode::NOP) {
        std::cout << "SUCCESS: MUL + ADD synthesized into single-cycle FMA.\n";
    } else {
        std::cout << "FAILURE: Fusion engine did not merge nodes.\n";
    }
}

int main() {
    std::cout << "===============================================\n";
    std::cout << "   DGTL FRAMEWORK STRESS SUITE v1.1\n";
    std::cout << "   Target: Better-than-LLVM Optimization\n";
    std::cout << "===============================================\n";
    
    run_dependency_chain_test();
    run_folding_test();
    run_fusion_test();

    std::cout << "\n--- Stress Test Suite Complete ---\n";
    return 0;
}
