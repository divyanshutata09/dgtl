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
#include <stdexcept>

// Core DGTL Headers
#include "dgtl/ir/Global_IR.hpp"
#include "dgtl/pass/transform/instruction_reordering.hpp"
#include "dgtl/analysis/Memory_Dependency_Smasher.hpp"
#include "dgtl/analysis/cost/AI_Cost_Model.hpp"
#include "dgtl/core/Verified_Formal.hpp" // Added for Verifier

using namespace dgtl;

/**
 * @class Instruction_Reordering_Pass_Testable
 * @brief Test-wrapper to expose internal optimization logic.
 */
class Instruction_Reordering_Pass_Testable : public dgtl::pass::transform::Instruction_Reordering_Pass {
public:
    Instruction_Reordering_Pass_Testable(dgtl::analysis::DGTLMemorySmasher& s, dgtl::analysis::DGTLAICostModel* ai)
        : dgtl::pass::transform::Instruction_Reordering_Pass(s, ai) {}
    
    const char* get_pass_name() const noexcept override {
        return "Instruction_Reordering_Pass_Test";
    }

    void test_internal_optimization(std::vector<dgtl::pass::transform::IR_Op>& block, const dgtl::ir::IRStream& stream) {
        this->run_optimization(block, stream);
    }
};

bool is_before(const std::vector<dgtl::pass::transform::IR_Op>& block, uint32_t first_id, uint32_t second_id) {
    int first_idx = -1, second_idx = -1;
    for (size_t i = 0; i < block.size(); ++i) {
        if (block[i].original_stream_idx == first_id) first_idx = (int)i;
        if (block[i].original_stream_idx == second_id) second_idx = (int)i;
    }
    return (first_idx != -1 && second_idx != -1 && first_idx < second_idx);
}

void test_latency_reordering() {
    std::cout << "[TEST] Starting Latency Reordering Analysis..." << std::endl;

    // Fix 1: Provide required dependencies to DGTLMemorySmasher
    dgtl::core::DGTLFormalVerifier verifier;
    dgtl::analysis::DGTLAICostModel ai_model;
    dgtl::analysis::DGTLMemorySmasher smasher(verifier, ai_model);
    
    dgtl::ir::IRStream stream; 
    
    // Fix 2: Correcting push_node to match BIR 2.1 specification (Op, Flags, Res, A, B, C)
    // Note: ensure OpCodes match your actual ir::OpCode enum definitions.
    
    // 0: LOAD (High Latency)
    stream.push_node(dgtl::ir::OpCode::LOAD, dgtl::ir::Flags::NONE, 10, 100, 0, 0); 
    
    // 1: OP_ADD (Low Latency - Dependent on Node 0 via register 10)
    stream.push_node(dgtl::ir::OpCode::OP_ADD, dgtl::ir::Flags::NONE, 11, 10, 5, 0);

    Instruction_Reordering_Pass_Testable pass(smasher, &ai_model);

    // 2. Create Adapter Block
    std::vector<dgtl::pass::transform::IR_Op> block;
    block.reserve(stream.size());
    block.emplace_back(0, stream.get_node_readonly(0));
    block.emplace_back(1, stream.get_node_readonly(1));

    // 3. Run Optimization
    pass.test_internal_optimization(block, stream);

    // Since 1 depends on 0 (RAW dependency), they should NOT swap despite latency heuristics.
    if (is_before(block, 0, 1)) {
        std::cout << "[PASS] Dependency integrity maintained." << std::endl;
    } else {
        throw std::runtime_error("FAIL: Reordering broke data dependency!");
    }
}

void run_Instruction_test_reordering() {
    try {
        test_latency_reordering();
        std::cout << "\n[SUCCESS] All Instruction Reordering Tests Passed." << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "[FAIL] Exception: " << e.what() << std::endl;
        return;
    }
    return;
}





