#include <iostream>

#include "dgtl/ir/Global_IR.hpp"
#include "dgtl/pass/GVN.hpp"
#include "dgtl/pass/SIMD_Legalizer.hpp"
#include "dgtl/isa/NASM_Emitter.hpp"
#include "dgtl/core/Arena.hpp"


using namespace dgtl;

int main() {
    std::cout << "--- [DGTL-TEST] 11_gather_16_strike ---" << std::endl;
    core::DGTLArena arena(1024 * 1024);
    ir::IRStream stream(arena, 1024);
    
    // TODO: Implement specific test logic for 11_gather_16_strike
    
    return 0;
}
