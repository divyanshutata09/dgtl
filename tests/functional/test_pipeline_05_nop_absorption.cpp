#include <iostream>
#include "dgtl/core/Arena.hpp"
#include "dgtl/ir/Global_IR.hpp"
using namespace dgtl;
int main() {
    core::DGTLArena a(2);
    ir::IRStream s(a, 16);
    s.push_node(ir::OpCode::NOP, ir::Flags::NONE, 0, 0, 0, 0);
    if(s.get_node(0).opcode == ir::OpCode::NOP) std::cout << "SUCCESS";
    return 0;
}
