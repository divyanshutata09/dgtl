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

#include "dgtl/dgtl_all.hpp"

#include <iostream>

void run_test_pipeline_12_ai_cost_rejection() {
    constexpr size_t kArenaMbPerThread = 2;
    constexpr size_t kReservedNodes = 64;
    constexpr dgtl::ir::Flags kGatherFlags =
        static_cast<dgtl::ir::Flags>(dgtl::ir::Flags::IS_SIMD_BURST | dgtl::ir::HintFlags::RequiresVecPort);

    std::cout << "--- [DGTL-TEST] 12_ai_cost_rejection ---\n";

    // Keep this test deterministic: 1 thread, explicit per-thread MB size.
    dgtl::core::DGTL_Arena arena(1, kArenaMbPerThread);
    dgtl::ir::IRStream stream(arena, kReservedNodes);
    dgtl::analysis::DGTLAICostModel model;

    stream.begin_transaction();
    stream.push_node(dgtl::ir::OpCode::ADD_I32, dgtl::ir::Flags::NONE, 1, 10, 11, 0, 0);
    stream.push_node(dgtl::ir::OpCode::GATHER_F32_512,
                     kGatherFlags,
                     2, 20, 21, 0, 0);
    stream.push_node(dgtl::ir::OpCode::RET, dgtl::ir::Flags::IS_RETURN, 0, 1, 0, 0, 0);
    stream.commit_transaction();

    const auto scalar = stream.get_node(0);
    const auto gather = stream.get_node(1);

    const float scalar_cost = model.predict_node_cost(scalar);
    const float gather_cost = model.predict_node_cost(gather);
    const float total_cost = model.predict_cost(stream);

    if (stream.size() == 3 && scalar_cost > 0.0f && gather_cost > scalar_cost && total_cost >= gather_cost) {
        std::cout << "SUCCESS\n";
        return;
    }

    std::cout << "FAIL\n";
    return;
}








