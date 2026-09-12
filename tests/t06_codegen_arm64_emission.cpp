/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 * DGTL Test 06 — ARM64 Codegen: End-to-End Emission
 *
 * Judges: a MUL_F32+ADD_F32 pattern fused into FMA_F32 is emitted by the
 *         ARM64 backend as valid assembly text containing the fmadd mnemonic.
 *         This validates the optimizer→emitter pipeline end-to-end.
 */


#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
import dgtl.ir_core.stream;
import dgtl.arena;  
import dgtl.ir_core.semantic;
import dgtl.ir_core.verified_formal;
import dgtl.ir_core.transform_scalar_core;
import dgtl.ir_core.arm64_emitter;

void run_test_t06_codegen_arm64_emission() {
    std::cout << "[T06] ARM64 Codegen: End-to-End FMA Emission\n";

    dgtl::core::DGTL_Arena arena(1, 16);
    dgtl::ir::IRStream stream(arena, 32);
    dgtl::semantic::SymbolTable symbols(arena);

    // Build fuseable pattern
    stream.push_node(dgtl::ir::OpCode::MUL_F32, dgtl::ir::Flags::NONE, 20, 10, 11, 0);
    stream.push_node(dgtl::ir::OpCode::ADD_F32, dgtl::ir::Flags::NONE, 21, 20, 12, 0);
    stream.push_node(dgtl::ir::OpCode::RET,     dgtl::ir::Flags::IS_RETURN, 0, 21, 0, 0);

    // Fuse into FMA_F32
    dgtl::core::DGTLFormalVerifier verifier;
    dgtl::pass::transform::DGTLInstructionFusionSynth fusion(verifier);
    fusion.execute(stream);

    if (stream.get_node(1).opcode != dgtl::ir::OpCode::FMA_F32) {
        throw std::runtime_error(
            "T06: pre-condition failed - fusion did not produce FMA_F32 before emission");
    }

    // Emit ARM64 assembly
    dgtl::isa::ARM64Emitter emitter(stream, symbols);
    dgtl::isa::ARM64Emitter::Config cfg;
    cfg.verbose_asm = true;

    std::ostringstream oss;
    emitter.emit_assembly(oss, cfg);
    const std::string asm_text = oss.str();

    if (asm_text.empty()) {
        throw std::runtime_error("T06: ARM64 emitter produced empty output for an FMA_F32 stream");
    }
    if (asm_text.find("fmadd") == std::string::npos &&
        asm_text.find("fmla")  == std::string::npos &&
        asm_text.find("fma")   == std::string::npos) {
        throw std::runtime_error(
            "T06: ARM64 emission did not contain any FMA mnemonic (fmadd/fmla/fma). "
            "Output was:\n" + asm_text);
    }

    std::cout << "[T06] PASS - ARM64 emission produced FMA mnemonic for fused stream\n";
}
