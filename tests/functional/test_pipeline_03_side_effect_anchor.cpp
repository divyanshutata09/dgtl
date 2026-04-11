#include <iostream>
#include "dgtl/core/Arena.hpp"
#include "dgtl/ir/Global_IR.hpp"
using namespace dgtl;
int main() {
    core::DGTLArena a(2);
    ir::IRStream s(a, 16);
    s.push_node(ir::OpCode::ADD_I32, ir::Flags::HAS_SIDE_EFFECTS, 1, 0, 0, 0);
    if(s.get_node(0).flags == ir::Flags::HAS_SIDE_EFFECTS) std::cout << "SUCCESS";
    return 0;
}
