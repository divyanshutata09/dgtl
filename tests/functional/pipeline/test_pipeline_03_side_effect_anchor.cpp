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
using namespace dgtl;
void run_test_pipeline_03_side_effect_anchor() {
    core::DGTL_Arena a(2);
    ir::IRStream s(a, 16);
    s.push_node(ir::OpCode::ADD_I32, ir::Flags::HAS_SIDE_EFFECTS, 1, 0, 0, 0);
    if(s.get_node(0).flags == ir::Flags::HAS_SIDE_EFFECTS) std::cout << "SUCCESS";
    return;
}




