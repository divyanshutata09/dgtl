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
#include <filesystem>
#include <string>

void run_test_frontend_payload_io() {
    constexpr dgtl::core::FrontendTypeHandle kI32 = 0x1001;
    constexpr uint32_t kValueIdLhs = 1;
    constexpr uint32_t kValueIdRhs = 2;
    constexpr uint32_t kValueIdAdd = 3;
    constexpr const char* kFunctionName = "payload_io";

    dgtl::core::FrontendPayloadModule payload;

    dgtl::core::FrontendLoweringFunction fn;
    fn.name = kFunctionName;
    fn.return_type_handle = kI32;
    fn.return_abi_class = dgtl::core::DGTLABIValueClass::i32;
    fn.parameters.push_back({kValueIdLhs, "lhs", kI32, dgtl::core::DGTLABIValueClass::i32});
    fn.parameters.push_back({kValueIdRhs, "rhs", kI32, dgtl::core::DGTLABIValueClass::i32});

    dgtl::core::FrontendLoweringInstruction add;
    add.kind = dgtl::core::FrontendInstructionKind::add_i32;
    add.result_value_id = kValueIdAdd;
    add.type_handle = kI32;
    add.lhs_value_id = kValueIdLhs;
    add.rhs_value_id = kValueIdRhs;

    dgtl::core::FrontendLoweringInstruction ret;
    ret.kind = dgtl::core::FrontendInstructionKind::ret;
    ret.ret_value_id = kValueIdAdd;

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
    const auto& loaded_fn = loaded.functions[0];
    assert(loaded_fn.name == kFunctionName);
    assert(loaded_fn.parameters.size() == 2);
    assert(loaded_fn.instructions.size() == 2);

    dgtl::core::DGTL_Arena arena(1, 8);
    dgtl::ir::IRStream lowered_stream(arena, 64);
    dgtl::core::DGTLFunction lowered_fn(arena, loaded_fn.name, 64);
    dgtl::core::DGTLControlFlowGraph cfg;
    dgtl::core::DGTLABIRegistry abi;
    const auto lowered_result = dgtl::core::FrontendPipelineLowerer::lower_function(
        loaded_fn, lowered_fn, cfg, &abi);
    assert(lowered_result.ok);
    assert(lowered_result.message.empty() || lowered_result.message.find("error") == std::string::npos);
    assert(lowered_result.emitted_nodes == lowered_fn.stream().size());

    lowered_stream.copy_from(lowered_fn.stream());
    assert(lowered_stream.size() == 2);
    assert(lowered_stream.get_node(0).opcode == dgtl::ir::OpCode::ADD_I32);
    assert(lowered_stream.get_node(1).opcode == dgtl::ir::OpCode::RET);
}
