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

#include <array>
#include <cassert>
#include <span>

void run_test_frontend_bridge_smoke() {
    constexpr dgtl::core::FrontendTypeHandle kI32 = 0x1001;
    constexpr uint8_t kUnknownAbiRegister = 0xFF;
    constexpr const char* kFunctionName = "frontend_bridge_smoke";
    constexpr const char* kCalleeName = "ext_add";

    dgtl::core::DGTL_Arena arena(1, 8);
    dgtl::core::DGTLFunction function(arena, kFunctionName, 64);
    dgtl::core::FrontendLoweringContext lowering(function);

    function.set_return_type(kI32);

    const uint32_t lhs_slot = function.add_parameter("lhs", kI32, dgtl::ir::OpCode::ADD_I32);
    const uint32_t rhs_slot = function.add_parameter("rhs", kI32, dgtl::ir::OpCode::ADD_I32);

    function.debug_store().record_loc(0, 10, 4, 1);
    function.debug_store().record_type(0, 1, 2, 0, 0);

    const uint32_t sum_slot = lowering.emit_binary(dgtl::ir::OpCode::ADD_I32, lhs_slot, rhs_slot, kI32);
    lowering.emit_return(sum_slot);

    dgtl::core::DGTLControlFlowGraph cfg;
    const uint32_t entry = cfg.create_block(0);
    const uint32_t exit = cfg.create_block(1);
    assert(cfg.add_edge(entry, exit));
    assert(cfg.seal_block(entry, 1));
    assert(cfg.seal_block(exit, function.stream().size()));
    assert(cfg.validate());

    dgtl::core::DGTLABIRegistry abi_registry;
    dgtl::core::DGTLCallSignature signature;
    signature.callee = kCalleeName;
    signature.calling_convention = dgtl::core::DGTLCallingConvention::dgtl_internal;
    signature.return_value = {"ret", dgtl::core::DGTLABIValueClass::i32, kUnknownAbiRegister, 0, kI32};
    signature.parameters.push_back({"lhs", dgtl::core::DGTLABIValueClass::i32, kUnknownAbiRegister, 0, kI32});
    signature.parameters.push_back({"rhs", dgtl::core::DGTLABIValueClass::i32, kUnknownAbiRegister, 0, kI32});
    abi_registry.register_signature(signature);

    const auto* resolved_signature = abi_registry.find_signature(kCalleeName);
    assert(resolved_signature != nullptr);
    assert(resolved_signature->parameters.size() == 2);

    assert(function.parameters().size() == 2);
    assert(function.return_type() == kI32);
    assert(function.stream().size() == 2);

    const auto first_node = function.stream().get_node(0);
    const auto second_node = function.stream().get_node(1);
    assert(first_node.opcode == dgtl::ir::OpCode::ADD_I32);
    assert(second_node.opcode == dgtl::ir::OpCode::RET);
    assert(second_node.op_a == sum_slot);

    const auto loc = function.debug_store().get_loc(0);
    const auto type = function.debug_store().get_type(0);
    assert(loc.line == 10);
    assert(loc.column == 4);
    assert(type.type_tag == 1);
    assert(type.ownership_flags == 2);

    const auto slot_type = function.get_slot_type(sum_slot);
    assert(slot_type.has_value());
    assert(*slot_type == kI32);

    const uint32_t callee_slot = function.define_or_get_local(kCalleeName, 0, dgtl::ir::OpCode::CALL);
    assert(callee_slot != dgtl::ir::INVALID_SLOT);

    const uint32_t call_result_slot = function.define_temp(kI32, dgtl::ir::OpCode::CALL);
    const std::array<uint32_t, 2> call_args{lhs_slot, rhs_slot};
    assert(lowering.emit_call(call_result_slot, callee_slot,
                              std::span<const uint32_t>(call_args.data(), call_args.size()),
                              resolved_signature));

    assert(function.stream().size() == 3);
    const auto call_node = function.stream().get_node(2);
    assert(call_node.opcode == dgtl::ir::OpCode::CALL);
    assert(call_node.result_id == call_result_slot);
    assert(call_node.op_a == callee_slot);
}