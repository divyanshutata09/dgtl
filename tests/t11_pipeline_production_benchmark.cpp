/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 * DGTL Test 11 — Production Pipeline Benchmark (Linked Build)
 *
 * Judges: end-to-end DGTL pipeline health in one run:
 *         IR build -> GVN -> macro fusion -> speculative DCE -> ARM64 emission.
 */


#include <chrono>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
import dgtl.ir_core.stream;
import dgtl.arena;
import dgtl.ir_core.semantic;
import dgtl.ir_core.verified_formal;
import dgtl.ir_core.transform_scalar_core;
import dgtl.ir_core.ai_cost_model;
import dgtl.ir_core.speculate_alias_dce_inliner;
import dgtl.ir_core.arm64_emitter;

void run_test_t11_pipeline_production_benchmark() {
    std::cout << "[T11] Production Pipeline Benchmark (IR->OPT->CODEGEN)\n";

    constexpr std::size_t kBlocks = 30'000;
    constexpr std::size_t kNodesPerBlock = 6;
    constexpr std::size_t kTotalNodes = kBlocks * kNodesPerBlock;

    dgtl::core::DGTL_Arena arena(1, 192);
    arena.pre_touch_all();
    dgtl::ir::IRStream stream(arena, kTotalNodes + 64);
    dgtl::semantic::SymbolTable symbols(arena);

    uint32_t next_id = 100;
    for (std::size_t i = 0; i < kBlocks; ++i) {
        const uint32_t a = next_id++;
        const uint32_t b = next_id++;
        const uint32_t c = next_id++;
        const uint32_t d = next_id++;
        const uint32_t e = next_id++;
        const uint32_t f = next_id++;

        stream.push_node(dgtl::ir::OpCode::LOAD_IMM, dgtl::ir::Flags::IS_CONST, a, 0, 0, 7);
        stream.push_node(dgtl::ir::OpCode::LOAD_IMM, dgtl::ir::Flags::IS_CONST, b, 0, 0, 9);
        stream.push_node(dgtl::ir::OpCode::ADD_I32,  dgtl::ir::Flags::IS_SPEC_CONST, c, a, b, 0);
        stream.push_node(dgtl::ir::OpCode::MUL_F32,  dgtl::ir::Flags::NONE, d, a, b, 0);
        stream.push_node(dgtl::ir::OpCode::ADD_F32,  dgtl::ir::Flags::NONE, e, d, b, 0);
        stream.push_node(dgtl::ir::OpCode::ADD_I32,  dgtl::ir::Flags::NONE, f, c, a, 0);
    }

    if (stream.size() != kTotalNodes) {
        throw std::runtime_error("T11: node construction mismatch, expected "
                                 + std::to_string(kTotalNodes)
                                 + ", got " + std::to_string(stream.size()));
    }
    if (!stream.validate_shadow_integrity()) {
        throw std::runtime_error("T11: pre-pipeline shadow integrity failed");
    }

    dgtl::core::DGTLFormalVerifier verifier;
    dgtl::pass::transform::DGTLInstructionFusionSynth fusion(verifier);
    dgtl::analysis::DGTLAICostModel model;
    dgtl::pass::DGTLSpeculativeDCE dce(model);

    const auto t0 = std::chrono::high_resolution_clock::now();

    dgtl::pass::transform::GlobalValueNumbering().execute(stream);
    fusion.execute(stream);
    dce.execute(stream);

    dgtl::isa::ARM64Emitter emitter(stream, symbols);
    dgtl::isa::ARM64Emitter::Config cfg;
    cfg.verbose_asm = false;
    std::ostringstream oss;
    emitter.emit_assembly(oss, cfg);

    const auto t1 = std::chrono::high_resolution_clock::now();

    if (!stream.validate_shadow_integrity()) {
        throw std::runtime_error("T11: post-pipeline shadow integrity failed");
    }

    const std::uint64_t hash = stream.generate_logic_hash();
    if (hash == 0) {
        throw std::runtime_error("T11: logic hash is zero after pipeline");
    }

    const std::string asm_text = oss.str();
    if (asm_text.empty()) {
        throw std::runtime_error("T11: ARM64 emitter produced empty output");
    }

    const auto us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
    const double seconds = static_cast<double>(us) * 1.0e-6;
    const double mnps = (seconds > 0.0)
        ? (static_cast<double>(kTotalNodes) / seconds / 1.0e6)
        : 0.0;

    std::cout << "[T11] Pipeline throughput: " << mnps << " M-nodes/sec ("
              << kTotalNodes << " nodes in " << us << " us)\n";

    constexpr double kMinPipelineMnps = 5.0;
    if (mnps < kMinPipelineMnps) {
        throw std::runtime_error("T11: end-to-end pipeline throughput too low: "
                                 + std::to_string(mnps)
                                 + " M-nodes/sec, required >= "
                                 + std::to_string(kMinPipelineMnps));
    }

    std::cout << "[T11] PASS - production pipeline benchmark qualified\n";
}
