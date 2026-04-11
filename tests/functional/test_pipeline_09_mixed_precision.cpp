#include <iostream>
#include "dgtl/core/Arena.hpp"
#include "dgtl/ir/Global_IR.hpp"

int main() {
    std::cout << "--- [DGTL-TEST] 09_mixed_precision ---" << std::endl;
    dgtl::core::DGTLArena arena(512); 
    dgtl::ir::IRStream stream(arena, 1024);

    stream.begin_transaction();
    
    // Mix I32 and F32 operations
    stream.push_node(dgtl::ir::OpCode::ADD_I32, dgtl::ir::Flags::NONE, 1, 10, 20, 0, 0);
    stream.push_node(dgtl::ir::OpCode::ADD_F32, dgtl::ir::Flags::IS_FUSED, 2, 5, 5, 0, 0);
    
    stream.commit_transaction();

    if (arena.get_checkpoint() > 0) {
        std::cout << "SUCCESS" << std::endl;
    } else {
        return 1;
    }
    return 0;
}
