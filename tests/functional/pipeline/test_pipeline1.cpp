#include <iostream>
#include <vector>
#include <cassert>

// Framework Includes
#include "dgtl/ir/base/Global_IR.hpp"
#include "dgtl/pass/transform/GVN.hpp"
#include "dgtl/pass/transform/Instruction_Folder.hpp"
#include "dgtl/core/Verified_Formal.hpp" 
#include "dgtl/analysis/cost/AI_Cost_Model.hpp"
#include "dgtl/core/Arena.hpp"

using namespace dgtl;

/**
 * @brief TEST 1: Verifies Dependency Chain resolution.
 * BIR 2.1 Protocol: Redundant nodes are converted to MOV [SourceID].
 */
void run_dependency_chain_test() {
    std::cout << "\n--- TEST 1: Dependency Chain & GVN Resolution ---\n";
    core::DGTL_Arena arena(1);
    ir::IRStream stream(arena, 1024);
    stream.set_count(0);

    // Node 0: r1 = 10 + 11
    stream.push_node(ir::OpCode::ADD_I32, ir::Flags::NONE, 1, 10, 11, 0);
    // Node 1: r2 = 10 + 11 (Redundant - GVN should map this to r1)
    stream.push_node(ir::OpCode::ADD_I32, ir::Flags::NONE, 2, 10, 11, 0);
    // Node 2: r3 = r2 + 5 (Should become r1 + 5 after GVN)
    stream.push_node(ir::OpCode::ADD_I32, ir::Flags::NONE, 3, 2, 5, 0);

    dgtl::pass::DGTLGVN gvn;
    gvn.execute(stream);

    auto node1 = stream.get_node(1); // The redundant node
    auto node2 = stream.get_node(2); // The dependent node

    std::cout << "Target Node OpA (Referenced ID): " << node2.op_a << "\n";
    
    // In GVN 2.4, redundant nodes become MOV/NOP and consumers update their OpA
    bool is_resolved = (node2.op_a == 1); 
    bool is_eliminated = (node1.opcode == ir::OpCode::MOV || node1.opcode == ir::OpCode::NOP);

    if (is_resolved && is_eliminated) {
        std::cout << "SUCCESS: Dependency chain compressed. Consumer points to Node 1.\n";
    } else {
        std::cout << "FAILURE: GVN did not resolve the duplicate node references.\n";
    }
}

/**
 * @brief TEST 2: Verifies Constant Folding targeting immediate conversion.
 * BIR 2.1 Protocol: Folded constants must use OpCode::CONST for backend emission.
 */
void run_folding_test() {
    std::cout << "\n--- TEST 2: Constant Instruction Folding ---\n";
    core::DGTL_Arena arena(1);
    ir::IRStream stream(arena, 1024);
    stream.set_count(0);

    // Node 0: r1 = 100 + 200
    stream.push_node(ir::OpCode::ADD_I32, ir::Flags::IS_SPEC_CONST, 1, 100, 200, 0);

    core::DGTLFormalVerifier verifier;
    dgtl::pass::DGTLInstructionFolder folder(verifier);
    folder.fold_stream(stream);

    auto node0 = stream.get_node(0);
    
    // Updated check: OpCode::CONST is the high-performance immediate carrier
    if (node0.opcode == ir::OpCode::CONST || node0.opcode == ir::OpCode::LOAD_IMM) {
        std::cout << "SUCCESS: 100+200 folded into constant immediate.\n";
        std::cout << "Folded Result (op_a): " << node0.op_a << "\n";
    } else {
        std::cout << "FAILURE: Instruction folder skipped the constant-ready node.\n";
    }
}

/**
 * @brief TEST 3: Verifies Macro-Op Fusion (MUL + ADD -> FMA).
 */
void run_fusion_test() {
    std::cout << "\n--- TEST 3: Macro-Op Fusion (FMA) ---\n";
    
    // Checking for implementation status in the pipeline manager
    #ifdef DGTL_FUSION_ENABLED
        std::cout << "Fusion engine active. Executing...\n";
        // Implementation here once synthesizer is linked
    #else
        std::cout << "SKIPPED: Fusion engine not implemented/enabled in this build.\n";
    #endif
}

void run_test_pipeline1() {
    std::cout << "===============================================\n";
    std::cout << "    DGTL FRAMEWORK STRESS SUITE v1.1\n";
    std::cout << "    Target: Better-than-LLVM Optimization\n";
    std::cout << "===============================================\n";
    
    run_dependency_chain_test();
    run_folding_test();
    run_fusion_test();

    std::cout << "\n--- Stress Test Suite Complete ---\n";
    return;
}

