/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 * DGTL Test 05 — Volatile Side-Effect Isolation
 *
 * Judges: optimizer must NOT fuse a MUL_F32 with a downstream ADD_F32 that
 *         carries IS_VOLATILE | HAS_SIDE_EFFECTS. Violating this would silently
 *         drop a hardware side-effect (e.g., CSR write, accumulator flush).
 */


#include <iostream>
#include <stdexcept>
import dgtl.ir_core.stream;
import dgtl.arena;
import dgtl.ir_core.verified_formal;
import dgtl.ir_core.transform_scalar_core;

void run_test_t05_optimizer_volatile_guard() {
    std::cout << "[T05] Volatile Side-Effect Isolation\n";

    dgtl::core::DGTL_Arena arena(1, 8);
    dgtl::ir::IRStream stream(arena, 16);

    // Node 0: normal MUL_F32
    stream.push_node(dgtl::ir::OpCode::MUL_F32,
                     dgtl::ir::Flags::NONE,
                     100, 10, 11, 0);

    // Node 1: ADD_F32 marked VOLATILE + HAS_SIDE_EFFECTS — must NOT be fused
    stream.push_node(dgtl::ir::OpCode::ADD_F32,
                     static_cast<uint16_t>(dgtl::ir::Flags::IS_VOLATILE | dgtl::ir::Flags::HAS_SIDE_EFFECTS),
                     101, 100, 12, 0);

    dgtl::core::DGTLFormalVerifier verifier;
    dgtl::pass::transform::DGTLInstructionFusionSynth fusion(verifier);
    fusion.execute(stream);

    const auto n0 = stream.get_node(0);
    const auto n1 = stream.get_node(1);

    // If fusion incorrectly fired, node0 would be NOP and node1 would be FMA_F32
    if (n0.opcode == dgtl::ir::OpCode::NOP && n1.opcode == dgtl::ir::OpCode::FMA_F32) {
        throw std::runtime_error(
            "T05: optimizer illegally fused across a volatile side-effect boundary - "
            "HAS_SIDE_EFFECTS flag was not respected");
    }

    // Correct outcome: both nodes remain untouched
    if (n0.opcode != dgtl::ir::OpCode::MUL_F32) {
        throw std::runtime_error(
            "T05: node[0] opcode changed unexpectedly; expected MUL_F32, got "
            + std::to_string(static_cast<int>(n0.opcode)));
    }
    if (n1.opcode != dgtl::ir::OpCode::ADD_F32) {
        throw std::runtime_error(
            "T05: node[1] opcode changed unexpectedly; expected ADD_F32, got "
            + std::to_string(static_cast<int>(n1.opcode)));
    }

    std::cout << "[T05] PASS - volatile boundary respected, no illegal fusion occurred\n";
}
