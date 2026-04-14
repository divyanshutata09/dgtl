#include <iostream>
#include "dgtl/core/Arena.hpp"
#include "dgtl/ir/base/Global_IR.hpp"
using namespace dgtl;
void run_test_pipeline_05_nop_absorption() {
    core::DGTL_Arena a(2);
    ir::IRStream s(a, 16);
    s.push_node(ir::OpCode::NOP, ir::Flags::NONE, 0, 0, 0, 0);
    if(s.get_node(0).opcode == ir::OpCode::NOP) std::cout << "SUCCESS";
    return;
}

