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

#include <iostream>
#include <stdexcept>

namespace {

void build_matmul_like_scop(dgtl::ir::IRStream& stream) {
    stream.reset();
    stream.begin_transaction();

    // Induction seeds: i, j, k
    stream.push_node(dgtl::ir::OpCode::LOAD_IMM, dgtl::ir::Flags::IS_CONST, 1, 0, 0, dgtl::ir::INVALID_SLOT, 0);
    stream.push_node(dgtl::ir::OpCode::LOAD_IMM, dgtl::ir::Flags::IS_CONST, 2, 0, 0, dgtl::ir::INVALID_SLOT, 0);
    stream.push_node(dgtl::ir::OpCode::LOAD_IMM, dgtl::ir::Flags::IS_CONST, 3, 0, 0, dgtl::ir::INVALID_SLOT, 0);

    // Accumulator for C[i][j]
    stream.push_node(dgtl::ir::OpCode::LOAD_IMM, dgtl::ir::Flags::IS_CONST, 10, 0, 0, dgtl::ir::INVALID_SLOT, 0);

    // A[i][k] and B[k][j] loads (affine-like register/index usage)
    stream.push_node(dgtl::ir::OpCode::LOAD, dgtl::ir::Flags::NONE, 11, 1, 3, dgtl::ir::INVALID_SLOT, 0);
    stream.push_node(dgtl::ir::OpCode::LOAD, dgtl::ir::Flags::NONE, 12, 3, 2, dgtl::ir::INVALID_SLOT, 0);

    // MAC: acc += A[i][k] * B[k][j]
    stream.push_node(dgtl::ir::OpCode::MUL_I32, dgtl::ir::Flags::NONE, 13, 11, 12, dgtl::ir::INVALID_SLOT, 0);
    stream.push_node(dgtl::ir::OpCode::ADD_I32, dgtl::ir::Flags::NONE, 10, 10, 13, dgtl::ir::INVALID_SLOT, 0);

    // Counter updates (self-referential updates feed loop-depth heuristic)
    stream.push_node(dgtl::ir::OpCode::ADD_I32, dgtl::ir::Flags::NONE, 3, 3, 1, dgtl::ir::INVALID_SLOT, 0); // k++
    stream.push_node(dgtl::ir::OpCode::ADD_I32, dgtl::ir::Flags::NONE, 2, 2, 1, dgtl::ir::INVALID_SLOT, 0); // j++
    stream.push_node(dgtl::ir::OpCode::ADD_I32, dgtl::ir::Flags::NONE, 1, 1, 1, dgtl::ir::INVALID_SLOT, 0); // i++

    stream.push_node(dgtl::ir::OpCode::STORE, dgtl::ir::Flags::NONE, dgtl::ir::INVALID_SLOT, 1, 2, 10, 0);
    stream.push_node(dgtl::ir::OpCode::BRANCH_COND, dgtl::ir::Flags::NONE, dgtl::ir::INVALID_SLOT, 2, 0, 0, 0);
    stream.push_node(dgtl::ir::OpCode::RET, dgtl::ir::Flags::IS_RETURN, 0, 10, dgtl::ir::INVALID_SLOT, dgtl::ir::INVALID_SLOT, 0);

    stream.commit_transaction();
}

size_t count_optimized_atoms(const dgtl::ir::IRStream& stream) {
    size_t count = 0;
    for (size_t i = 0; i < stream.size(); ++i) {
        const auto atom = stream.get_atom_readonly(i);
        if (atom.flags & dgtl::ir::Flags::IS_OPTIMIZED) {
            ++count;
        }
    }
    return count;
}

} // namespace

void run_test_poly_matmul_scop_tiling() {
    constexpr size_t kArenaMbPerThread = 128;
    constexpr size_t kReservedNodes = 128;
    constexpr uint32_t kTileSize = 32;

    std::cout << "--- POLYHEDRAL SCoP 2D MATMUL TILING ---\n";

    dgtl::core::DGTL_Arena arena(1, kArenaMbPerThread);

    dgtl::ir::IRStream stream(arena, kReservedNodes);
    build_matmul_like_scop(stream);

    dgtl::graph::ExecutionGraph graph;
    graph.build_from_ir(stream);

    const dgtl::graph::Region* tile_candidate = nullptr;
    for (const auto& region : graph.regions) {
        if (!region) {
            continue;
        }
        if (region->scop_annotation.affinity == dgtl::graph::ScopAffinity::AFFINE_CANDIDATE &&
            region->scop_annotation.loop_depth >= 2 &&
            dgtl::pass::DGTLPolyTransformer::can_tile_region(*region)) {
            tile_candidate = region.get();
            break;
        }
    }

    if (tile_candidate == nullptr) {
        throw std::runtime_error("matmul-like region did not trigger tileable affine SCoP classification");
    }

    const size_t optimized_before = count_optimized_atoms(stream);

    dgtl::pass::LoopNest nest{};
    nest.depth = 2;
    nest.induction_vars[0] = 1;
    nest.induction_vars[1] = 2;
    nest.lower_bounds[0] = 0;
    nest.lower_bounds[1] = 0;
    nest.upper_bounds[0] = 64;
    nest.upper_bounds[1] = 64;
    nest.strides[0] = 1;
    nest.strides[1] = 1;

    dgtl::pass::DGTLPolyTransformer transformer;
    transformer.apply_tiling(stream, nest, kTileSize);

    const size_t optimized_after = count_optimized_atoms(stream);
    if (optimized_after <= optimized_before) {
        throw std::runtime_error("polyhedral tiling did not mark optimized atoms");
    }

    // Determinism check: same IR + same tiling config should produce same final hash.
    dgtl::ir::IRStream replay(arena, kReservedNodes);
    build_matmul_like_scop(replay);
    transformer.apply_tiling(replay, nest, kTileSize);

    if (stream.generate_logic_hash() != replay.generate_logic_hash()) {
        throw std::runtime_error("tiling transform is not deterministic for identical input");
    }

    std::cout << "PASS: matmul-like affine SCoP triggered and tiling path committed deterministically.\n";
}
