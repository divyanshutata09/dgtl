#include <iostream>
#include "dgtl/core/Arena.hpp"
#include "dgtl/ir/Global_IR.hpp"
#include "dgtl/pass/SIMD_Legalizer.hpp"
using namespace dgtl;
int main() {
    core::DGTLArena a(2);
    ir::IRStream s(a, 16);
    s.begin_strike();
    for(int i=0; i<4; ++i) s.push_node(ir::OpCode::ADD_I32, ir::Flags::NONE, 100+i, i, i, 0);
    s.commit_strike();
    opt::DGTLSIMDLegalizer l(opt::TargetArch::X86_AVX512);
    l.legalize(s);
    if(s.get_node(0).opcode == static_cast<ir::OpCode>(0x204)) std::cout << "SUCCESS";
    return 0;
}
