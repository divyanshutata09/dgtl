/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 */

import dgtl.arena;
import dgtl.ir;
import dgtl.ir.pass.transform_scalar_core;

#include <iostream>
#include <stdexcept>

void run_test_t03_optimization_smoke() {
    std::cout << "[T03] Optimization smoke\n";

    dgtl::core::DGTL_Arena arena(1, 2);
    dgtl::ir::IRStream stream(arena, 64);

    stream.push_node(dgtl::ir::OpCode::LOAD_IMM, dgtl::ir::Flags::IS_CONST, 10, 0, 0, 50);
    stream.push_node(dgtl::ir::OpCode::LOAD_IMM, dgtl::ir::Flags::IS_CONST, 11, 0, 0, 50);
    stream.push_node(dgtl::ir::OpCode::ADD_I32, dgtl::ir::Flags::NONE, 1, 10, 11, 0);
    stream.push_node(dgtl::ir::OpCode::ADD_I32, dgtl::ir::Flags::NONE, 2, 10, 11, 0);
    stream.push_node(dgtl::ir::OpCode::ADD_I32, dgtl::ir::Flags::NONE, 3, 2, 10, 0);
    stream.push_node(dgtl::ir::OpCode::RET, dgtl::ir::Flags::IS_RETURN, 0, 3, dgtl::ir::INVALID_SLOT, dgtl::ir::INVALID_SLOT);

    const size_t before = stream.size();

    dgtl::ir::pass::transform::GlobalValueNumbering().execute(stream);
    dgtl::ir::pass::transform::DeadCodeElimination().execute(stream);

    if (stream.size() == 0 || stream.size() > before) {
        throw std::runtime_error("T03: unexpected node growth after optimization");
    }
    if (!stream.validate_shadow_integrity()) {
        throw std::runtime_error("T03: integrity failure after GVN+DCE");
    }
    if (stream.get_node(stream.size() - 1).opcode != dgtl::ir::OpCode::RET) {
        throw std::runtime_error("T03: optimized stream must end with RET");
    }

    std::cout << "[T03] PASS - optimization smoke OK\n";
}
