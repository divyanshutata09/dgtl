#include <iostream>

#include "dgtl/core/Arena.hpp"
#include "dgtl/ir/Global_IR.hpp"


int main() {
    std::cout << "--- [DGTL-TEST] 15_rollback_gate ---" << std::endl;
    dgtl::core::DGTLArena arena(512); 
    dgtl::ir::IRStream stream(arena, 1024);

    size_t start_checkpoint = arena.get_checkpoint();

    stream.begin_transaction();
    stream.push_node(dgtl::ir::OpCode::STORE, dgtl::ir::Flags::NONE, 0, 100, 200, 0, 0);
    
    // FAIL THE TRANSACTION
    stream.rollback_transaction();

    // The checkpoint should have returned to start_checkpoint
    if (arena.get_checkpoint() == start_checkpoint) {
        std::cout << "SUCCESS" << std::endl;
    } else {
        std::cout << "FAIL: Arena leak detected after rollback!" << std::endl;
        return 1;
    }
    return 0;
}
