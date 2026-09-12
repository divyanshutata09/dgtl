/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 * DGTL Test 03 — GVN: Constant Folding and Duplicate Elimination
 *
 * Judges: GVN collapses 100+200 into an immediate node (CONST or LOAD_IMM),
 *         and remaps a downstream consumer's operand from the duplicate to
 *         the canonical result.
 */
#include <iostream>
#include <stdexcept>
import dgtl.ir_core.stream;
import dgtl.arena;
import dgtl.ir_core.transform_scalar_core;



void run_test_t03_optimizer_gvn_const_fold() {
    std::cout << "[T03] GVN: Constant Folding\n";

    dgtl::core::DGTL_Arena arena(1, 16);
    dgtl::ir::IRStream stream(arena, 64);

    // Seed constants: reg10 = 100, reg11 = 200
    stream.push_node(dgtl::ir::OpCode::LOAD_IMM, dgtl::ir::Flags::IS_CONST, 10, 0, 0, 100);
    stream.push_node(dgtl::ir::OpCode::LOAD_IMM, dgtl::ir::Flags::IS_CONST, 11, 0, 0, 200);
    // r1 = reg10 + reg11  (IS_SPEC_CONST signals to GVN it is foldable)
    stream.push_node(dgtl::ir::OpCode::ADD_I32, dgtl::ir::Flags::IS_SPEC_CONST, 1, 10, 11, 0);

    dgtl::pass::transform::GlobalValueNumbering().execute(stream);

    // Find the node with result_id == 1
    bool found = false;
    for (size_t i = 0; i < stream.size(); ++i) {
        const auto n = stream.get_node(i);
        if (n.result_id == 1) {
            if (n.opcode != dgtl::ir::OpCode::CONST &&
                n.opcode != dgtl::ir::OpCode::LOAD_IMM) {
                throw std::runtime_error(
                    "T03: GVN did not fold 100+200 into a constant node; opcode="
                    + std::to_string(static_cast<int>(n.opcode)));
            }
            found = true;
            break;
        }
    }
    if (!found) {
        throw std::runtime_error("T03: result node (result_id=1) is missing after GVN - incorrectly eliminated");
    }

    // Duplicate elimination: two identical ADDs must resolve to same canonical node
    dgtl::ir::IRStream s2(arena, 64);
    s2.push_node(dgtl::ir::OpCode::LOAD_IMM, dgtl::ir::Flags::IS_CONST, 10, 0, 0, 50);
    s2.push_node(dgtl::ir::OpCode::LOAD_IMM, dgtl::ir::Flags::IS_CONST, 11, 0, 0, 50);
    s2.push_node(dgtl::ir::OpCode::ADD_I32,  dgtl::ir::Flags::NONE,       1, 10, 11, 0);
    s2.push_node(dgtl::ir::OpCode::ADD_I32,  dgtl::ir::Flags::NONE,       2, 10, 11, 0); // duplicate
    s2.push_node(dgtl::ir::OpCode::ADD_I32,  dgtl::ir::Flags::NONE,       3,  2, 10, 0); // uses dup

    dgtl::pass::transform::GlobalValueNumbering().execute(s2);

    // node with result_id=3 should have op_a remapped to 1 (canonical) not 2 (duplicate)
    for (size_t i = 0; i < s2.size(); ++i) {
        const auto n = s2.get_node(i);
        if (n.result_id == 3) {
            if (n.op_a != 1) {
                throw std::runtime_error(
                    "T03: GVN did not remap duplicate consumer op_a from 2->1; op_a="
                    + std::to_string(n.op_a));
            }
            break;
        }
    }

    std::cout << "[T03] PASS - constant folding and duplicate elimination correct\n";
}
