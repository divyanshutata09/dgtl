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

#include "dgtl/all.hpp"

#include <cassert>

void run_test_frontend_pipeline_lowerer() {
    dgtl::core::DGTL_Arena arena(1, 8);
    dgtl::core::DGTLFunction function(arena, "frontend_pipeline_lowerer", 96);
    dgtl::core::DGTLControlFlowGraph cfg;
    dgtl::core::DGTLABIRegistry abi_registry;

    dgtl::core::FrontendLoweringFunction frontend_fn;
    frontend_fn.name = "frontend_pipeline_lowerer";
    frontend_fn.return_type_handle = 0x1001;
    frontend_fn.return_abi_class = dgtl::core::DGTLABIValueClass::i32;

    frontend_fn.parameters.push_back({1, "lhs", 0x1001, dgtl::core::DGTLABIValueClass::i32});
    frontend_fn.parameters.push_back({2, "rhs", 0x1001, dgtl::core::DGTLABIValueClass::i32});

    dgtl::core::FrontendLoweringInstruction add_ins;
    add_ins.kind = dgtl::core::FrontendInstructionKind::add_i32;
    add_ins.result_value_id = 3;
    add_ins.type_handle = 0x1001;
    add_ins.lhs_value_id = 1;
    add_ins.rhs_value_id = 2;

    dgtl::core::FrontendLoweringInstruction ret_ins;
    ret_ins.kind = dgtl::core::FrontendInstructionKind::ret;
    ret_ins.ret_value_id = 3;

    frontend_fn.instructions.push_back(add_ins);
    frontend_fn.instructions.push_back(ret_ins);

    const auto result = dgtl::core::FrontendPipelineLowerer::lower_function(frontend_fn,
                                                                            function,
                                                                            cfg,
                                                                            &abi_registry);

    assert(result.ok);
    assert(result.emitted_nodes == function.stream().size());
    assert(function.stream().size() == 2);

    const auto first = function.stream().get_node(0);
    const auto second = function.stream().get_node(1);
    assert(first.opcode == dgtl::ir::OpCode::ADD_I32);
    assert(second.opcode == dgtl::ir::OpCode::RET);

    const auto* sig = abi_registry.find_signature("frontend_pipeline_lowerer");
    assert(sig != nullptr);
    assert(sig->parameters.size() == 2);

    dgtl::core::FrontendLoweringFunction bad_fn;
    bad_fn.name = "bad";
    bad_fn.instructions.push_back(dgtl::core::FrontendLoweringInstruction{
        dgtl::core::FrontendInstructionKind::ret,
        dgtl::ir::INVALID_SLOT,
        0,
        0,
        dgtl::ir::INVALID_SLOT,
        dgtl::ir::INVALID_SLOT,
        {},
        {},
        9999
    });

    dgtl::core::DGTLFunction bad_function(arena, "bad", 32);
    dgtl::core::DGTLControlFlowGraph bad_cfg;
    const auto bad = dgtl::core::FrontendPipelineLowerer::lower_function(bad_fn, bad_function, bad_cfg, nullptr);
    assert(!bad.ok);
}
