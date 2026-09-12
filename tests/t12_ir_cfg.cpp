/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 * DGTL Test 12 - IR CFG reconstruction
 *
 * Judges: basic-block discovery, edge classification, and loop back-edge
 *         reconstruction from a linear IR stream.
 */


#include <iostream>
#include <sstream>
#include <stdexcept>

import dgtl.ir_core.stream;
import dgtl.arena;
import dgtl.ir_core.io;
import dgtl.ir_core.arm64_emitter;

namespace {

bool has_edge(const std::vector<dgtl::ir::CFGEdge>& edges,
              uint32_t from,
              uint32_t to,
              dgtl::ir::CFGEdgeKind kind) {
    for (const auto& edge : edges) {
        if (edge.from == from && edge.to == to && edge.kind == kind) {
            return true;
        }
    }
    return false;
}

} // namespace

void run_test_t12_ir_cfg() {
    std::cout << "[T12] IR CFG reconstruction\n";

    dgtl::core::DGTL_Arena arena(1, 8);
    dgtl::ir::IRStream stream(arena, 32);

    stream.push_node(dgtl::ir::OpCode::LABEL, dgtl::ir::Flags::NONE, 100, 0, 0, 0);
    stream.push_node(dgtl::ir::OpCode::LOAD_IMM, dgtl::ir::Flags::IS_CONST, 10, 0, 0, 1);
    stream.push_node(dgtl::ir::OpCode::BRANCH_COND,
                     dgtl::ir::Flags::NONE,
                     0,
                     10,
                     200,
                     dgtl::ir::INVALID_SLOT);

    stream.push_node(dgtl::ir::OpCode::LABEL, dgtl::ir::Flags::NONE, 300, 0, 0, 0);
    stream.push_node(dgtl::ir::OpCode::ADD_I32, dgtl::ir::Flags::NONE, 11, 10, 10, 0);
    stream.push_node(dgtl::ir::OpCode::BRANCH, dgtl::ir::Flags::NONE, 0, 100, 0, 0);

    stream.push_node(dgtl::ir::OpCode::LABEL, dgtl::ir::Flags::NONE, 200, 0, 0, 0);
    stream.push_node(dgtl::ir::OpCode::RET, dgtl::ir::Flags::IS_RETURN, 0, 11, 0, 0);

    const auto& blocks = stream.get_basic_blocks();
    const auto& edges = stream.get_cfg_edges();

    if (blocks.size() != 3) {
        throw std::runtime_error("T12: expected 3 CFG blocks, got " + std::to_string(blocks.size()));
    }

    if (!blocks[0].is_entry || blocks[0].primary_label != 100 || blocks[0].begin != 0 || blocks[0].end != 3) {
        throw std::runtime_error("T12: entry block shape mismatch");
    }
    if (blocks[1].primary_label != 300 || blocks[1].begin != 3 || blocks[1].end != 6) {
        throw std::runtime_error("T12: loop body block shape mismatch");
    }
    if (blocks[2].primary_label != 200 || blocks[2].begin != 6 || blocks[2].end != 8) {
        throw std::runtime_error("T12: exit block shape mismatch");
    }

    if (!has_edge(edges, 0, 2, dgtl::ir::CFGEdgeKind::ConditionalTrue)) {
        throw std::runtime_error("T12: missing conditional true edge from header to exit");
    }
    if (!has_edge(edges, 0, 1, dgtl::ir::CFGEdgeKind::ConditionalFalse)) {
        throw std::runtime_error("T12: missing conditional false edge from header to loop body");
    }
    if (!has_edge(edges, 1, 0, dgtl::ir::CFGEdgeKind::UnconditionalBranch)) {
        throw std::runtime_error("T12: missing loop back-edge from body to header");
    }

    if (stream.get_block_id_for_node(5) != 1 || stream.get_block_id_for_node(7) != 2) {
        throw std::runtime_error("T12: node-to-block mapping mismatch");
    }

    std::ostringstream printed;
    dgtl::ir::IRPrinter::Options print_opts;
    print_opts.show_cfg = true;
    dgtl::ir::IRPrinter::print(stream, printed, print_opts);
    const std::string cfg_dump = printed.str();
    if (cfg_dump.find("=== CFG | blocks: 3 | edges: 3 ===") == std::string::npos) {
        throw std::runtime_error("T12: IRPrinter CFG header missing from dump");
    }
    if (cfg_dump.find("-> b0 [branch]") == std::string::npos) {
        throw std::runtime_error("T12: IRPrinter CFG back-edge missing from dump");
    }

    std::cout << "[T12] PASS - CFG blocks, edges, back-edge, and printer output OK\n";
}