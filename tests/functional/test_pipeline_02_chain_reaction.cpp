#include <iostream>
#include "dgtl/core/Arena.hpp"
#include "dgtl/ir/Global_IR.hpp"
using namespace dgtl;
int main() {
    core::DGTLArena a(2);
    ir::IRStream s(a, 16);
    s.begin_strike();
    s.push_node(ir::OpCode::ADD_I32, ir::Flags::NONE, 1, 0, 0, 0);
    s.push_node(ir::OpCode::ADD_I32, ir::Flags::NONE, 2, 1, 0, 0);
    s.commit_strike();
    if(s.size() == 2) std::cout << "SUCCESS";
    return 0;
}
