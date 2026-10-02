/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 * DGTL Test 02 â€” IR Transactional Commit and Rollback
 *
 * Judges: begin/commit preserves nodes, begin/rollback discards nodes,
 *         shadow integrity holds after both operations.
 */



#include <iostream>
#include <stdexcept>
import dgtl.ir.stream;
import dgtl.arena;

void run_test_t02_ir_transaction_rollback() {
    std::cout << "[T02] IR Transaction Commit and Rollback\n";

    dgtl::core::DGTL_Arena arena(1, 8);
    dgtl::ir::IRStream stream(arena, 64);

    // -- baseline: push two nodes without a transaction --
    stream.push_node(dgtl::ir::OpCode::LOAD_IMM, dgtl::ir::Flags::IS_CONST, 10, 0, 0, 5);
    stream.push_node(dgtl::ir::OpCode::LOAD_IMM, dgtl::ir::Flags::IS_CONST, 11, 0, 0, 7);
    const size_t baseline = stream.size(); // expected: 2
    if (baseline != 2) {
        throw std::runtime_error("T02: baseline size should be 2, got "
                                 + std::to_string(baseline));
    }

    // -- COMMIT path --
    stream.begin_transaction();
    stream.push_node(dgtl::ir::OpCode::ADD_I32, dgtl::ir::Flags::NONE, 1, 10, 11, 0);
    stream.commit_transaction();

    if (stream.size() != 3) {
        throw std::runtime_error("T02: after commit, size should be 3, got "
                                 + std::to_string(stream.size()));
    }
    const auto committed = stream.get_node(2);
    if (committed.opcode != dgtl::ir::OpCode::ADD_I32 || committed.result_id != 1) {
        throw std::runtime_error("T02: committed node[2] does not match expected ADD_I32 result_id=1");
    }
    if (!stream.validate_shadow_integrity()) {
        throw std::runtime_error("T02: shadow integrity failed after commit");
    }

    // -- ROLLBACK path --
    stream.begin_transaction();
    stream.push_node(dgtl::ir::OpCode::MUL_I32, dgtl::ir::Flags::NONE, 99, 10, 11, 0);
    stream.push_node(dgtl::ir::OpCode::MUL_I32, dgtl::ir::Flags::NONE, 98, 10, 11, 0);
    // size is now 5 transiently
    stream.rollback_transaction();

    if (stream.size() != 3) {
        throw std::runtime_error("T02: after rollback, size should revert to 3, got "
                                 + std::to_string(stream.size()));
    }
    if (!stream.validate_shadow_integrity()) {
        throw std::runtime_error("T02: shadow integrity failed after rollback");
    }
    // ensure the rolled-back nodes are gone
    const auto n2_after = stream.get_node(2);
    if (n2_after.opcode != dgtl::ir::OpCode::ADD_I32) {
        throw std::runtime_error("T02: node[2] opcode changed after rollback - core stream corrupted");
    }

    std::cout << "[T02] PASS - commit preserved nodes, rollback discarded nodes, integrity OK\n";
}
