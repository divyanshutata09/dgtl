#include <iostream>
#include "dgtl/core/Arena.hpp"
#include "dgtl/ir/Global_IR.hpp"
using namespace dgtl;

int main() {
    core::DGTLArena a(2);
    ir::IRStream s(a, 16);
    
    // Testing the result_id storage for register 1001
    s.push_node(ir::OpCode::ADD_I32, ir::Flags::NONE, 1001, 10, 20, 0);
    
    auto node = s.get_node(0);
    
    // Using the 'result_id' field as defined in IRNode
    if(node.result_id == 1001) {
        std::cout << "SUCCESS";
    }
    
    return 0;
}
