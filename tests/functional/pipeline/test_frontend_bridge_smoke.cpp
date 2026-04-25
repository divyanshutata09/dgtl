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

void run_test_frontend_bridge_smoke() {
    dgtl::core::DGTL_Arena arena(1, 8);
    dgtl::core::DGTLFunction function(arena, "frontend_bridge_smoke", 64);
    dgtl::core::FrontendLoweringContext lowering(function);

    function.set_return_type(0x1001);

    const uint32_t lhs_slot = function.add_parameter("lhs", 0x1001, dgtl::ir::OpCode::ADD_I32);
    const uint32_t rhs_slot = function.add_parameter("rhs", 0x1001, dgtl::ir::OpCode::ADD_I32);

    function.debug_store().record_loc(0, 10, 4, 1);
    function.debug_store().record_type(0, 1, 2, 0, 0);

    const uint32_t sum_slot = lowering.emit_binary(dgtl::ir::OpCode::ADD_I32, lhs_slot, rhs_slot, 0x1001);
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
    signature.callee = "frontend_bridge_smoke";
    signature.calling_convention = dgtl::core::DGTLCallingConvention::dgtl_internal;
    signature.return_value = {"ret", dgtl::core::DGTLABIValueClass::i32, 0xFF, 0, 0x1001};
    signature.parameters.push_back({"lhs", dgtl::core::DGTLABIValueClass::i32, 0xFF, 0, 0x1001});
    signature.parameters.push_back({"rhs", dgtl::core::DGTLABIValueClass::i32, 0xFF, 0, 0x1001});
    abi_registry.register_signature(signature);

    const auto* resolved_signature = abi_registry.find_signature("frontend_bridge_smoke");
    assert(resolved_signature != nullptr);
    assert(resolved_signature->parameters.size() == 2);

    assert(function.parameters().size() == 2);
    assert(function.return_type() == 0x1001);
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
    assert(*slot_type == 0x1001);

    std::span<const uint32_t> empty_args;
    assert(lowering.emit_call(function.define_temp(0x1001), lhs_slot, empty_args, nullptr));
    assert(function.stream().size() == 3);
}