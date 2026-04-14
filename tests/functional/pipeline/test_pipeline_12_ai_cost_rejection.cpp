#include <iostream>
#include "dgtl/ir/base/Global_IR.hpp"
#include "dgtl/pass/transform/GVN.hpp"
#include "dgtl/pass/simd/SIMD_Legalizer.hpp"
#include "dgtl/isa/emitter/NASM_Emitter.hpp"
#include "dgtl/core/Arena.hpp"
#include "dgtl/analysis/cost/AI_Cost_Model.hpp"

using namespace dgtl;

void run_test_pipeline_12_ai_cost_rejection() {
    std::cout << "--- [DGTL-TEST] 12_ai_cost_rejection ---" << std::endl;
    core::DGTL_Arena arena(64);
    ir::IRStream stream(arena, 64);
    dgtl::analysis::DGTLAICostModel model;

    stream.begin_transaction();
    stream.push_node(ir::OpCode::ADD_I32, ir::Flags::NONE, 1, 10, 11, 0, 0);
    stream.push_node(ir::OpCode::GATHER_F32_512,
                     ir::Flags::IS_SIMD_BURST | ir::Flags::REQUIRES_VEC_PORT,
                     2, 20, 21, 0, 0);
    stream.push_node(ir::OpCode::RET, ir::Flags::IS_RETURN, 0, 1, 0, 0, 0);
    stream.commit_transaction();

    const auto scalar = stream.get_node(0);
    const auto gather = stream.get_node(1);

    const float scalar_cost = model.predict_node_cost(scalar);
    const float gather_cost = model.predict_node_cost(gather);
    const float total_cost = model.predict_cost(stream);

    if (scalar_cost > 0.0f && gather_cost > scalar_cost && total_cost >= gather_cost) {
        std::cout << "SUCCESS" << std::endl;
        return;
    }

    std::cout << "FAIL" << std::endl;
    return;
}


