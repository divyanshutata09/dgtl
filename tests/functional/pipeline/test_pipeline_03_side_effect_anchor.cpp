#include <iostream>
#include "dgtl/core/Arena.hpp"
#include "dgtl/ir/base/Global_IR.hpp"
using namespace dgtl;
void run_test_pipeline_03_side_effect_anchor() {
    core::DGTL_Arena a(2);
    ir::IRStream s(a, 16);
    s.push_node(ir::OpCode::ADD_I32, ir::Flags::HAS_SIDE_EFFECTS, 1, 0, 0, 0);
    if(s.get_node(0).flags == ir::Flags::HAS_SIDE_EFFECTS) std::cout << "SUCCESS";
    return;
}

