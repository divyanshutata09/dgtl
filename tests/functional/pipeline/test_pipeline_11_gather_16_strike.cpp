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

#include <iostream>

#include "dgtl/ir/Global_IR.hpp"
#include "dgtl/pass/transform/GVN.hpp"
#include "dgtl/pass/simd/SIMD_Legalizer.hpp"
#include "dgtl/isa/NASM_Emitter.hpp"
#include "dgtl/core/Arena.hpp"
#include "dgtl/core/Symbol_Table.hpp"


using namespace dgtl;

void run_test_pipeline_11_gather_16_strike() {
    std::cout << "--- [DGTL-TEST] 11_gather_16_strike ---" << std::endl;
    core::DGTL_Arena arena(64);
    ir::IRStream stream(arena, 64);

    stream.begin_transaction();
    stream.push_node(ir::OpCode::GATHER_16,
                     ir::Flags::IS_SIMD_BURST | ir::Flags::REQUIRES_VEC_PORT,
                     10, 1, 2, 0, 0);
    stream.push_node(ir::OpCode::RET, ir::Flags::IS_RETURN, 0, 10, 0, 0, 0);
    stream.commit_transaction();

    dgtl::pass::DGTLGVN gvn;
    gvn.execute(stream);

    dgtl::isa::DGTLNasmEmitter emitter;
    dgtl::core::SymbolTable symbols;
    emitter.emit(stream, symbols);

    const auto asm_out = emitter.get_output();
    const auto n0 = stream.get_node(0);

    if (stream.size() >= 1 && n0.opcode == ir::OpCode::GATHER_16 && !asm_out.empty()) {
        std::cout << "SUCCESS" << std::endl;
        return;
    }

    std::cout << "FAIL" << std::endl;
    return;
}








