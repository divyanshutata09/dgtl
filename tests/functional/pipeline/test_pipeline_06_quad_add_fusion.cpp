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
#include "dgtl/core/Arena.hpp"
#include "dgtl/ir/Global_IR.hpp"
#include "dgtl/pass/simd/SIMD_Legalizer.hpp"
using namespace dgtl;
void run_test_pipeline_06_quad_add_fusion() {
    core::DGTL_Arena a(2);
    ir::IRStream s(a, 16);
    s.begin_transaction();
    for (uint32_t i = 0u; i < 4u; ++i) {
        s.push_node(ir::OpCode::ADD_I32, ir::Flags::NONE, 100u + i, i, i, 0u);
    }
    s.commit_transaction();
    dgtl::pass::DGTLSIMDLegalizer l(dgtl::pass::TargetArch::X86_AVX512);
    l.legalize(s);
    if(s.get_node(0).opcode == static_cast<ir::OpCode>(0x204)) std::cout << "SUCCESS";
    return;
}








