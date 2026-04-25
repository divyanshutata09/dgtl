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
#include <vector>
#include "dgtl/ir/Global_IR.hpp"
#include "dgtl/core/Arena.hpp"
#include "dgtl/pass/simd/SIMD_Legalizer.hpp"

using namespace dgtl;

void setup_test_stream(ir::IRStream& s) {
    s.begin_transaction();
    // Create 4 independent adds that SHOULD be fused
    for (uint32_t i = 0u; i < 4u; ++i) {
        s.push_node(ir::OpCode::ADD_I32, ir::Flags::NONE, 100u + i, i, i + 1u, 0u);
    }
    s.commit_transaction();
}

void run_test_pipeline_multi_target() {
    struct Case { dgtl::pass::TargetArch arch; uint32_t expected; std::string name; };
    std::vector<Case> cases = {
        { dgtl::pass::TargetArch::X86_AVX512,  0x204, "X86_AVX512" },
        { dgtl::pass::TargetArch::ARM_NEON,    0x304, "ARM_NEON" },
        { dgtl::pass::TargetArch::WASM_SIMD128, 0x404, "WASM_SIMD128" }
    };

    bool all_opt = true;

    for (auto& c : cases) {
        core::DGTL_Arena a(2);
        ir::IRStream s(a, 16);
        setup_test_stream(s);

        dgtl::pass::DGTLSIMDLegalizer legalizer(c.arch);
        legalizer.legalize(s);

        uint32_t result_op = static_cast<uint32_t>(s.get_node(0).opcode);
        if (result_op == c.expected) {
            std::cout << "[opt] " << c.name << " fused to 0x" << std::hex << c.expected << std::dec << std::endl;
        } else {
            std::cout << "[FAIL] " << c.name << " expected 0x" << std::hex << c.expected 
                      << " but got 0x" << result_op << std::dec << std::endl;
            all_opt = false;
        }
    }

    if(all_opt) std::cout << "GLOBAL_SUCCESS";
    return;
}








