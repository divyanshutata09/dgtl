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
#include <span>
#include <string>
#include <vector>

void run_test_frontend_link_api_emit() {
    dgtl::core::DGTL_Arena arena(1, 8);
    dgtl::core::DGTLFunction function(arena, "frontend_link_api_emit", 96);
    dgtl::core::DGTLControlFlowGraph cfg;
    dgtl::core::DGTLABIRegistry abi_registry;
    dgtl::core::DGTLFrontendLinkAPI link_api(function, cfg, &abi_registry);

    constexpr dgtl::core::FrontendTypeHandle kI32 = 0x1001;
    function.set_return_type(kI32);

    const uint32_t lhs = link_api.add_parameter("lhs", kI32, dgtl::ir::OpCode::ADD_I32, dgtl::core::DGTLABIValueClass::i32);
    const uint32_t rhs = link_api.add_parameter("rhs", kI32, dgtl::ir::OpCode::ADD_I32, dgtl::core::DGTLABIValueClass::i32);

    const uint32_t first_local = link_api.define_local("acc", kI32, dgtl::ir::OpCode::ADD_I32);
    const uint32_t second_local = link_api.define_local("acc", kI32, dgtl::ir::OpCode::ADD_I32);
    assert(first_local == second_local);

    const uint32_t sum = link_api.emit_binary(dgtl::ir::OpCode::ADD_I32, lhs, rhs, kI32);
    link_api.emit_return(sum);

    const uint32_t entry = link_api.create_block(0);
    const uint32_t exit = link_api.create_block(function.stream().size());
    assert(link_api.add_edge(entry, exit));
    assert(link_api.seal_block(entry, function.stream().size()));
    assert(link_api.seal_block(exit, function.stream().size()));
    assert(link_api.validate_cfg());

    const auto abi_params = link_api.abi_parameters();
    assert(abi_params.size() == 2);

    abi_registry.register_internal_signature(
        "ext_add",
        dgtl::core::DGTLABIValueClass::i32,
        kI32,
        std::span<const dgtl::core::DGTLABIParameter>(abi_params.data(), abi_params.size()),
        false);

    const auto* signature = abi_registry.find_signature("ext_add");
    assert(signature != nullptr);
    assert(signature->parameters.size() == 2);

    const uint32_t call_result = function.define_temp(kI32, dgtl::ir::OpCode::CALL);
    const std::vector<uint32_t> call_args{lhs, rhs};
    std::string call_error;
    assert(link_api.emit_call(call_result, "ext_add", std::span<const uint32_t>(call_args.data(), call_args.size()), &call_error));
    assert(call_error.empty());

    const std::vector<uint32_t> bad_call_args{lhs};
    assert(!link_api.emit_call(call_result, "ext_add", std::span<const uint32_t>(bad_call_args.data(), bad_call_args.size()), &call_error));
    assert(!call_error.empty());

    const auto call_node = function.stream().get_node(function.stream().size() - 1);
    assert(call_node.opcode == dgtl::ir::OpCode::CALL);
    assert(call_node.op_a != dgtl::ir::INVALID_SLOT);

    auto driver_request = dgtl::core::make_backend_driver_request(
        "Z:/__dgtl_missing__/dgtl_release.exe",
        dgtl::core::BackendDriverTarget::arm64,
        "build/out/frontend_link_api_emit.arm64.s");

    std::string validation_error;
    assert(!dgtl::core::validate_backend_driver_request(driver_request, &validation_error));
    assert(!validation_error.empty());

    dgtl::core::FrontendEmitRequest single_emit;
    single_emit.dgtl_executable_path = "Z:/__dgtl_missing__/dgtl_release.exe";
    single_emit.module_name = "frontend_link_api_emit";
    single_emit.output_dir = "build/out";
    single_emit.target = dgtl::core::BackendDriverTarget::arm64;

    const auto single_result = dgtl::core::emit_frontend_module(single_emit);
    assert(!single_result.ok);
    assert(single_result.per_target.size() == 1);

    dgtl::core::FrontendEmitRequest all_emit;
    all_emit.dgtl_executable_path = "Z:/__dgtl_missing__/dgtl_release.exe";
    all_emit.module_name = "frontend_link_api_emit";
    all_emit.output_dir = "build/out";
    all_emit.emit_all = true;

    const auto all_result = dgtl::core::emit_frontend_module(all_emit);
    assert(!all_result.ok);
    assert(all_result.per_target.size() == dgtl::core::all_backend_driver_targets().size());
}
