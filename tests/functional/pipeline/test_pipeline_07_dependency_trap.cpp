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
void run_test_pipeline_07_dependency_trap() {
    core::DGTL_Arena a(2);
    ir::IRStream s(a, 16);
    s.begin_transaction();
    s.push_node(ir::OpCode::ADD_I32, ir::Flags::NONE, 1, 10, 11, 0);
    s.push_node(ir::OpCode::ADD_I32, ir::Flags::NONE, 2, 1, 13, 0);
    s.commit_transaction();
    dgtl::pass::DGTLSIMDLegalizer l(dgtl::pass::TargetArch::X86_AVX512);
    l.legalize(s);
    int active=0;
    for(size_t i=0; i<s.size(); ++i) if(s.get_node(i).opcode != ir::OpCode::NOP) active++;
    if(active==2) std::cout << "SUCCESS";
    return;
}








