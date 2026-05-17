/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 * DGTL Test 04 — Macro-Op Fusion: MUL_F32 + ADD_F32 → FMA_F32
 *
 * Judges: DGTLInstructionFusionSynth correctly fuses a producer MUL_F32 and
 *         its dependent consumer ADD_F32 into a single FMA_F32 node with
 *         IS_MACRO_FUSED flag. The original producer must become NOP.
 */

#include "dgtl/ir/base/IR_Stream.hpp"
#include "dgtl/core/Arena.hpp"
#include "dgtl/core/Verified_Formal.hpp"
#include "dgtl/pass/transform/Transform_Scalar_Core.hpp"

#include <iostream>
#include <stdexcept>

void run_test_t04_optimizer_fusion() {
    std::cout << "[T04] Macro-Op Fusion: MUL_F32 + ADD_F32 -> FMA_F32\n";

    dgtl::core::DGTL_Arena arena(1, 8);
    dgtl::ir::IRStream stream(arena, 16);

    // Pattern: r20 = MUL_F32(r10, r11)
    //          r21 = ADD_F32(r20, r12)
    stream.push_node(dgtl::ir::OpCode::MUL_F32, dgtl::ir::Flags::NONE, 20, 10, 11, 0);
    stream.push_node(dgtl::ir::OpCode::ADD_F32, dgtl::ir::Flags::NONE, 21, 20, 12, 0);

    dgtl::core::DGTLFormalVerifier verifier;
    dgtl::pass::transform::DGTLInstructionFusionSynth fusion(verifier);
    fusion.execute(stream);

    const auto n0 = stream.get_node(0);
    const auto n1 = stream.get_node(1);

    if (n0.opcode != dgtl::ir::OpCode::NOP) {
        throw std::runtime_error(
            "T04: producer MUL_F32 was NOT NOP'd after fusion — fusion did not fire");
    }
    if (n1.opcode != dgtl::ir::OpCode::FMA_F32) {
        throw std::runtime_error(
            "T04: consumer was not promoted to FMA_F32; opcode="
            + std::to_string(static_cast<int>(n1.opcode)));
    }
    if (n1.result_id != 21) {
        throw std::runtime_error(
            "T04: FMA_F32 result_id mismatch — expected 21, got "
            + std::to_string(n1.result_id));
    }
    if (!(n1.flags & dgtl::ir::Flags::IS_MACRO_FUSED)) {
        throw std::runtime_error("T04: FMA_F32 node is missing IS_MACRO_FUSED flag");
    }

    std::cout << "[T04] PASS — MUL_F32+ADD_F32 fused into FMA_F32 with correct flags\n";
}
