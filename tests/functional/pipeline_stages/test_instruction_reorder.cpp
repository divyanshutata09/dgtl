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

import dgtl;
import dgtl.arena;
import dgtl.ir.stream;
import dgtl.ir_enums;
import dgtl.ir_node;

#include <cassert>
#include <iostream>
#include <stdexcept>
#include <vector>

/**
 * @class Instruction_Reordering_Pass_Testable
 * @brief Test-wrapper to expose internal optimization logic.
 */
class Instruction_Reordering_Pass_Testable : public dgtl::ir::pass::transform::Instruction_Reordering_Pass {
public:
    Instruction_Reordering_Pass_Testable(dgtl::analysis::DGTLMemorySmasher& smasher,
                                         dgtl::analysis::DGTLAICostModel* ai_model)
        : dgtl::ir::pass::transform::Instruction_Reordering_Pass(smasher, ai_model) {}
    
    const char* get_pass_name() const noexcept override {
        return "Instruction_Reordering_Pass_Test";
    }

    void test_internal_optimization(std::vector<dgtl::ir::pass::transform::IR_Op>& block, const dgtl::ir::IRStream& stream) {
        this->run_optimization(block, stream);
    }
};

bool is_before(const std::vector<dgtl::ir::pass::transform::IR_Op>& block,
               uint32_t first_id,
               uint32_t second_id) {
    int first_idx = -1;
    int second_idx = -1;
    for (size_t i = 0; i < block.size(); ++i) {
        if (block[i].original_stream_idx == first_id) {
            first_idx = static_cast<int>(i);
        }
        if (block[i].original_stream_idx == second_id) {
            second_idx = static_cast<int>(i);
        }
    }
    return first_idx != -1 && second_idx != -1 && first_idx < second_idx;
}

void test_latency_reordering() {
    constexpr size_t kReservedNodes = 16;
    constexpr uint32_t kLoadResult = 10;
    constexpr uint32_t kAddResult = 11;
    constexpr uint32_t kLoadAddress = 100;
    constexpr uint32_t kAddImmediate = 5;

    std::cout << "[TEST] Starting Latency Reordering Analysis...\n";

    // Reordering pass requires verifier and AI cost model via memory smasher.
    dgtl::core::DGTLFormalVerifier verifier;
    dgtl::analysis::DGTLAICostModel ai_model;
    dgtl::analysis::DGTLMemorySmasher smasher(verifier, ai_model);

    dgtl::core::DGTL_Arena arena(1, 64);
    dgtl::ir::IRStream stream(arena, kReservedNodes);
    
    // 0: LOAD (High Latency)
    stream.push_node(dgtl::ir::OpCode::LOAD,
                     dgtl::ir::Flags::NONE,
                     kLoadResult,
                     kLoadAddress,
                     0,
                     0);
    
    // 1: ADD_I32 (Low Latency - Dependent on Node 0 via register 10)
    stream.push_node(dgtl::ir::OpCode::ADD_I32,
                     dgtl::ir::Flags::NONE,
                     kAddResult,
                     kLoadResult,
                     kAddImmediate,
                     0);

    Instruction_Reordering_Pass_Testable pass(smasher, &ai_model);

    // Build adapter block expected by the reordering pass.
    std::vector<dgtl::ir::pass::transform::IR_Op> block;
    block.reserve(stream.size());
    block.emplace_back(0, stream.get_node_readonly(0));
    block.emplace_back(1, stream.get_node_readonly(1));

    pass.test_internal_optimization(block, stream);

    // Since 1 depends on 0 (RAW dependency), they should NOT swap despite latency heuristics.
    if (is_before(block, 0, 1)) {
        std::cout << "[PASS] Dependency integrity maintained.\n";
    } else {
        throw std::runtime_error("FAIL: Reordering broke data dependency!");
    }
}

void run_Instruction_test_reordering() {
    try {
        test_latency_reordering();
        std::cout << "\n[SUCCESS] All Instruction Reordering Tests Passed.\n";
    } catch (const std::exception& e) {
        std::cerr << "[FAIL] Exception: " << e.what() << '\n';
        return;
    }
    return;
}





