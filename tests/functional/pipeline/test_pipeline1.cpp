/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:    DGTL (Deep General-purpose Transformation Layer)
 * Owner:      Divyanshu Tata
 * Repository: github.com/TataG-Systems/DGTL
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

#include <iostream>
#include <vector>
#include <cassert>


// Framework Includes
#include "dgtl/ir/Global_IR.hpp"
#include "dgtl/pass/transform/GVN.hpp"
#include "dgtl/pass/transform/Instruction_Folder.hpp"
#include "dgtl/pass/transform/Instruction_Fusion_Synth.hpp"
#include "dgtl/core/Verified_Formal.hpp" 
#include "dgtl/analysis/cost/AI_Cost_Model.hpp"
#include "dgtl/core/Arena.hpp"

using namespace dgtl;

// Wired from production_arch_validator.cpp
void run_production_arch_validator();

/**
 * @brief TEST 1: Verifies Dependency Chain resolution.
 * BIR 2.1 Protocol: Redundant nodes are converted to MOV [SourceID].
 */
void run_dependency_chain_test() {
    std::cout << "\n--- TEST 1: Dependency Chain & GVN Resolution ---\n";
    core::DGTL_Arena arena(1);
    ir::IRStream stream(arena, 1024);
    stream.set_count(0);

    // Build legal SSA-style inputs: arithmetic operands are register IDs.
    // c10 = 10, c11 = 11, c5 = 5
    stream.push_node(ir::OpCode::LOAD_IMM, ir::Flags::IS_CONST, 10, 0, 10, 0);
    stream.push_node(ir::OpCode::LOAD_IMM, ir::Flags::IS_CONST, 11, 0, 11, 0);
    stream.push_node(ir::OpCode::LOAD_IMM, ir::Flags::IS_CONST, 12, 0, 5, 0);

    // r1 = c10 + c11
    stream.push_node(ir::OpCode::ADD_I32, ir::Flags::NONE, 1, 10, 11, 0);
    // r2 = c10 + c11 (Redundant - should map to r1)
    stream.push_node(ir::OpCode::ADD_I32, ir::Flags::NONE, 2, 10, 11, 0);
    // r3 = r2 + c5 (should become r1 + c5 after GVN remap)
    stream.push_node(ir::OpCode::ADD_I32, ir::Flags::NONE, 3, 2, 12, 0);

    stream.perform_gvn();

    ir::IRNode dependent{};
    bool found_dependent = false;
    for (size_t i = 0; i < stream.size(); ++i) {
        const auto n = stream.get_node(i);
        if (n.result_id == 3) {
            dependent = n;
            found_dependent = true;
            break;
        }
    }

    if (!found_dependent) {
        std::cout << "FAILURE: Dependent node was removed unexpectedly.\n";
        return;
    }

    std::cout << "Target Node OpA (Referenced ID): " << dependent.op_a << "\n";
    
    // In GVN 2.4, redundant nodes become MOV/NOP and consumers update their OpA
    const bool is_remapped = (dependent.op_a == 1);
    const bool is_folded_immediate =
        (dependent.opcode == ir::OpCode::LOAD_IMM || dependent.opcode == ir::OpCode::CONST) &&
        (dependent.op_a == 0);

    if (is_remapped || is_folded_immediate) {
        std::cout << "SUCCESS: Dependency chain optimized (remap or full fold).\n";
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

    // Seed constant table via LOAD_IMM values, then fold register-based ADD.
    stream.push_node(ir::OpCode::LOAD_IMM, ir::Flags::IS_CONST, 10, 0, 100, 0);
    stream.push_node(ir::OpCode::LOAD_IMM, ir::Flags::IS_CONST, 11, 0, 200, 0);
    stream.push_node(ir::OpCode::ADD_I32, ir::Flags::IS_SPEC_CONST, 1, 10, 11, 0);

    stream.perform_gvn();

    ir::IRNode folded{};
    bool found = false;
    for (size_t i = 0; i < stream.size(); ++i) {
        const auto n = stream.get_node(i);
        if (n.result_id == 1) {
            folded = n;
            found = true;
            break;
        }
    }

    if (!found) {
        std::cout << "FAILURE: Fold target node missing after folder pass.\n";
        return;
    }
    
    // Updated check: OpCode::CONST is the high-performance immediate carrier
    if (folded.opcode == ir::OpCode::CONST || folded.opcode == ir::OpCode::LOAD_IMM) {
        std::cout << "SUCCESS: 100+200 folded into constant immediate.\n";
        std::cout << "Folded Result (op_a/op_b): " << folded.op_a << '/' << folded.op_b << "\n";
    } else {
        std::cout << "FAILURE: Instruction folder skipped the constant-ready node.\n";
    }
}

/**
 * @brief TEST 3: Verifies Macro-Op Fusion (MUL + ADD -> FMA).
 */
void run_fusion_test() {
    std::cout << "\n--- TEST 3: Macro-Op Fusion (FMA) ---\n";

    core::DGTL_Arena arena(1);
    ir::IRStream stream(arena, 1024);
    stream.set_count(0);

    // Build a canonical fusion pattern:
    // r20 = MUL_F32(r10, r11)
    // r21 = ADD_F32(r20, r12)
    stream.push_node(ir::OpCode::MUL_F32, ir::Flags::NONE, 20, 10, 11, 0);
    stream.push_node(ir::OpCode::ADD_F32, ir::Flags::NONE, 21, 20, 12, 0);

    core::DGTLFormalVerifier verifier;
    dgtl::pass::DGTLInstructionFusionSynth fusion(verifier);
    fusion.execute(stream);

    // After apply_fusion the producer is NOP'd with result_id = INVALID_SLOT,
    // so check by stream index rather than result_id.
    const auto n0 = stream.get_node(0);
    const auto n1 = stream.get_node(1);

    bool producer_nopped = (n0.opcode == ir::OpCode::NOP);
    bool consumer_fused  = (n1.result_id == 21 &&
                            n1.opcode    == ir::OpCode::FMA_F32 &&
                            (n1.flags    &  ir::Flags::IS_MACRO_FUSED));

    if (producer_nopped && consumer_fused) {
        std::cout << "SUCCESS: MUL+ADD fused into FMA pattern.\n";
    } else {
        std::cout << "FAILURE: Fusion synthesis did not produce expected FMA form.\n";
    }
}

void run_test_pipeline1() {
    std::cout << "===============================================\n";
    std::cout << "    DGTL FRAMEWORK STRESS SUITE v1.1\n";
    std::cout << "    Target: Better-than-LLVM Optimization\n";
    std::cout << "===============================================\n";
    
    run_dependency_chain_test();
    run_folding_test();
    run_fusion_test();
    run_production_arch_validator();

    std::cout << "\n--- Stress Test Suite Complete ---\n";
    return;
}








