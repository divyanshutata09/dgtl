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

#include <cassert>
#include <string>

void run_test_frontend_pipeline_lowerer() {
    constexpr dgtl::core::FrontendTypeHandle kI32 = 0x1001;
    constexpr uint32_t kValueIdLhs = 1;
    constexpr uint32_t kValueIdRhs = 2;
    constexpr uint32_t kValueIdAdd = 3;
    constexpr uint32_t kInvalidReturnValueId = 9999;
    constexpr const char* kFunctionName = "frontend_pipeline_lowerer";
    constexpr const char* kBadFunctionName = "bad";

    dgtl::core::DGTL_Arena arena(1, 8);
    dgtl::core::DGTLFunction function(arena, kFunctionName, 96);
    dgtl::core::DGTLControlFlowGraph cfg;
    dgtl::core::DGTLABIRegistry abi_registry;

    dgtl::core::FrontendLoweringFunction frontend_fn;
    frontend_fn.name = kFunctionName;
    frontend_fn.return_type_handle = kI32;
    frontend_fn.return_abi_class = dgtl::core::DGTLABIValueClass::i32;

    frontend_fn.parameters.push_back({kValueIdLhs, "lhs", kI32, dgtl::core::DGTLABIValueClass::i32});
    frontend_fn.parameters.push_back({kValueIdRhs, "rhs", kI32, dgtl::core::DGTLABIValueClass::i32});

    dgtl::core::FrontendLoweringInstruction add_ins;
    add_ins.kind = dgtl::core::FrontendInstructionKind::add_i32;
    add_ins.result_value_id = kValueIdAdd;
    add_ins.type_handle = kI32;
    add_ins.lhs_value_id = kValueIdLhs;
    add_ins.rhs_value_id = kValueIdRhs;

    dgtl::core::FrontendLoweringInstruction ret_ins;
    ret_ins.kind = dgtl::core::FrontendInstructionKind::ret;
    ret_ins.ret_value_id = kValueIdAdd;

    frontend_fn.instructions.push_back(add_ins);
    frontend_fn.instructions.push_back(ret_ins);

    const auto result = dgtl::core::FrontendPipelineLowerer::lower_function(frontend_fn,
                                                                            function,
                                                                            cfg,
                                                                            &abi_registry);

    assert(result.ok);
    assert(result.message.empty() || result.message.find("error") == std::string::npos);
    assert(result.emitted_nodes == function.stream().size());
    assert(function.stream().size() == 2);

    const auto first = function.stream().get_node(0);
    const auto second = function.stream().get_node(1);
    assert(first.opcode == dgtl::ir::OpCode::ADD_I32);
    assert(second.opcode == dgtl::ir::OpCode::RET);

    const auto* sig = abi_registry.find_signature(kFunctionName);
    assert(sig != nullptr);
    assert(sig->parameters.size() == 2);

    dgtl::core::FrontendLoweringFunction bad_fn;
    bad_fn.name = kBadFunctionName;
    dgtl::core::FrontendLoweringInstruction bad_ret_ins;
    bad_ret_ins.kind = dgtl::core::FrontendInstructionKind::ret;
    bad_ret_ins.ret_value_id = kInvalidReturnValueId;
    bad_fn.instructions.push_back(bad_ret_ins);

    dgtl::core::DGTLFunction bad_function(arena, kBadFunctionName, 32);
    dgtl::core::DGTLControlFlowGraph bad_cfg;
    const auto bad = dgtl::core::FrontendPipelineLowerer::lower_function(bad_fn, bad_function, bad_cfg, nullptr);
    assert(!bad.ok);
    assert(!bad.message.empty());
}
