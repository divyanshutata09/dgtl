#include <iostream>
#include "dgtl/core/Arena.hpp"
#include "dgtl/ir/Global_IR.hpp"
#include "dgtl/pass/SIMD_Legalizer.hpp"
using namespace dgtl;
int main() {
    core::DGTLArena a(2);
    ir::IRStream s(a, 16);
    s.begin_strike();
    s.push_node(ir::OpCode::ADD_I32, ir::Flags::NONE, 1, 10, 11, 0);
    s.push_node(ir::OpCode::ADD_I32, ir::Flags::NONE, 2, 1, 13, 0);
    s.commit_strike();
    opt::DGTLSIMDLegalizer l(opt::TargetArch::X86_AVX512);
    l.legalize(s);
    int active=0;
    for(size_t i=0; i<s.size(); ++i) if(s.get_node(i).opcode != ir::OpCode::NOP) active++;
    if(active==2) std::cout << "SUCCESS";
    return 0;
}
