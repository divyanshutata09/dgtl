#include <iostream>
#include <cassert>

#include "dgtl/core/Arena.hpp"
#include "dgtl/ir/Global_IR.hpp"  


int main() {
    std::cout << "--- [DGTL-TEST] 08_partial_window ---" << std::endl;

    // 1. Initialize Arena
    dgtl::core::DGTLArena arena(256); 
    
    // 2. Simulate Partial Window
    // Use (void) to silence the [[nodiscard]] warning correctly
    (void)arena.allocate<uint64_t>(128); 
    size_t pre_offset = arena.get_checkpoint();

    // 3. Initialize IRStream
    dgtl::ir::IRStream stream(arena, 1024);
    
    // 4. Execute Transaction
    stream.begin_transaction();
    
    // FIX: Removed 'uint32_t id =' because your push_node returns void
    stream.push_node(
        dgtl::ir::OpCode::ADD_I32, 
        dgtl::ir::Flags::NONE,     
        1,                         
        10,                        
        20,                        
        0,                         
        0                          
    );
    
    stream.commit_transaction();

    // 5. Verify logic
    // We check if the arena offset advanced, which proves the node was written.
    if (arena.get_checkpoint() > pre_offset) {
        std::cout << "SUCCESS" << std::endl;
    } else {
        std::cout << "FAIL: Arena offset did not advance after push_node." << std::endl;
        return 1;
    }

    return 0;
}
