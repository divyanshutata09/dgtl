#include <iostream>
#include "dgtl/ir/Global_IR.hpp"
#include "dgtl/core/Arena.hpp"

int main() {
    std::cout << "--- [DGTL-TEST] 36_jit_specialization ---" << std::endl;
    dgtl::core::DGTLArena arena(512); 
    dgtl::ir::IRStream stream(arena, 1024);

    stream.begin_transaction();
    
    // Push a Guard Node: If this fails at runtime, it triggers a deoptimization
    stream.push_node(
        dgtl::ir::OpCode::GUARD_VALUE, 
        dgtl::ir::Flags::REQUIRES_DEOPT_TARGET, 
        0, 0, 0, 0, 0xBEEF // Intent Hash (the value we are guarding)
    );
    
    stream.commit_transaction();

    if (arena.get_checkpoint() > 0) {
        std::cout << "SUCCESS" << std::endl;
    } else {
        return 1;
    }
    return 0;
}
