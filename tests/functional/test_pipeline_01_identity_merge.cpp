#include <iostream>
#include "dgtl/core/Arena.hpp"
#include "dgtl/ir/Global_IR.hpp"

using namespace dgtl;
int main() {
    core::DGTLArena a(2);
    ir::IRStream s(a, 16);
    s.push_node(ir::OpCode::ADD_I32, ir::Flags::NONE, 10, 1, 1, 0);
    if(s.size() == 1) std::cout << "SUCCESS";
    return 0;
}
