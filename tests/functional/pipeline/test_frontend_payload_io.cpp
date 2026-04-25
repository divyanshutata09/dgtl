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
#include <filesystem>

void run_test_frontend_payload_io() {
    dgtl::core::FrontendPayloadModule payload;

    dgtl::core::FrontendLoweringFunction fn;
    fn.name = "payload_io";
    fn.return_type_handle = 0x1001;
    fn.return_abi_class = dgtl::core::DGTLABIValueClass::i32;
    fn.parameters.push_back({1, "lhs", 0x1001, dgtl::core::DGTLABIValueClass::i32});
    fn.parameters.push_back({2, "rhs", 0x1001, dgtl::core::DGTLABIValueClass::i32});

    dgtl::core::FrontendLoweringInstruction add;
    add.kind = dgtl::core::FrontendInstructionKind::add_i32;
    add.result_value_id = 3;
    add.type_handle = 0x1001;
    add.lhs_value_id = 1;
    add.rhs_value_id = 2;

    dgtl::core::FrontendLoweringInstruction ret;
    ret.kind = dgtl::core::FrontendInstructionKind::ret;
    ret.ret_value_id = 3;

    fn.instructions.push_back(add);
    fn.instructions.push_back(ret);
    payload.functions.push_back(fn);

    const std::filesystem::path out = std::filesystem::path("build") / "tests" / "frontend_payload_io.dgtl.payload";

    std::string error;
    assert(dgtl::core::write_frontend_payload_file(payload, out.string(), &error));
    assert(error.empty());

    dgtl::core::FrontendPayloadModule loaded;
    assert(dgtl::core::read_frontend_payload_file(out.string(), loaded, &error));
    assert(error.empty());

    assert(loaded.functions.size() == 1);
    assert(loaded.functions[0].name == "payload_io");
    assert(loaded.functions[0].parameters.size() == 2);
    assert(loaded.functions[0].instructions.size() == 2);

    dgtl::core::DGTL_Arena arena(1, 8);
    dgtl::ir::IRStream lowered_stream(arena, 64);
    dgtl::core::DGTLFunction lowered_fn(arena, loaded.functions[0].name, 64);
    dgtl::core::DGTLControlFlowGraph cfg;
    dgtl::core::DGTLABIRegistry abi;
    const auto lowered_result = dgtl::core::FrontendPipelineLowerer::lower_function(
        loaded.functions[0], lowered_fn, cfg, &abi);
    assert(lowered_result.ok);

    lowered_stream.copy_from(lowered_fn.stream());
    assert(lowered_stream.size() == 2);
    assert(lowered_stream.get_node(0).opcode == dgtl::ir::OpCode::ADD_I32);
    assert(lowered_stream.get_node(1).opcode == dgtl::ir::OpCode::RET);
}
