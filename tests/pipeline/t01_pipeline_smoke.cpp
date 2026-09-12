/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 */

#include "dgtl/core/Arena.hpp"
#include "dgtl/ir/Global_IR.hpp"
#include "dgtl/pass/transform/Transform_Scalar_Core.hpp"

#include <iostream>
#include <stdexcept>

import dgtl.arena;
import dgtl.ir_stream;
import dgtl.ir_core.transform_scalar_core;


void run_test_t01_pipeline_smoke() {
    std::cout << "[T01] Pipeline smoke\n";

    dgtl::core::DGTL_Arena arena(1, 2);
    dgtl::ir::IRStream stream(arena, 64);

    stream.begin_transaction();
    stream.push_node(dgtl::ir::OpCode::ADD_I32, dgtl::ir::Flags::NONE, 100, 10, 11, 0);
    stream.push_node(dgtl::ir::OpCode::ADD_I32, dgtl::ir::Flags::NONE, 101, 10, 11, 0);
    stream.push_node(dgtl::ir::OpCode::SYSCALL, dgtl::ir::Flags::NONE, 0, 101, 0, 0);
    stream.push_node(dgtl::ir::OpCode::RET, dgtl::ir::Flags::IS_RETURN, 0, 100, 0, 0);
    stream.commit_transaction();

    dgtl::pass::transform::GlobalValueNumbering().execute(stream);
    dgtl::pass::transform::DeadCodeElimination().execute(stream);

    if (!stream.validate_shadow_integrity()) {
        throw std::runtime_error("T01: integrity failure after optimization");
    }
    if (stream.size() < 2) {
        throw std::runtime_error("T01: pipeline collapsed unexpectedly");
    }
    if (stream.get_node(stream.size() - 1).opcode != dgtl::ir::OpCode::RET) {
        throw std::runtime_error("T01: pipeline must terminate with RET");
    }

    std::cout << "[T01] PASS - pipeline smoke OK\n";
}
