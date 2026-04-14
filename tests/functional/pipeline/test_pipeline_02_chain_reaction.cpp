#include <iostream>
#include "dgtl/core/Arena.hpp"
#include "dgtl/ir/base/Global_IR.hpp"
using namespace dgtl;
void run_test_pipeline_02_chain_reaction() {
    core::DGTL_Arena a(2);
    ir::IRStream s(a, 16);
    s.begin_transaction();
    s.push_node(ir::OpCode::ADD_I32, ir::Flags::NONE, 1, 0, 0, 0);
    s.push_node(ir::OpCode::ADD_I32, ir::Flags::NONE, 2, 1, 0, 0);
    s.commit_transaction();
    if(s.size() == 2) std::cout << "SUCCESS";
    return;
}

