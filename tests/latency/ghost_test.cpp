#include <iostream>
#include "dgtl/ir/Global_IR.hpp"
#include "dgtl/core/Arena.hpp"


int main() {
    // Initialize Arena and Stream
    dgtl::core::DGTLArena arena(5); 
    dgtl::ir::IRStream stream(arena, 1000);

    std::cout << "\033[1;36m[DGTL-GHOST]\033[0m Testing Transactional Atomicity..." << std::endl;

    // 1. Commit a valid Base Node
    stream.begin_transaction();
    stream.push_node(dgtl::ir::OpCode::ADD_I32, dgtl::ir::Flags::NONE, 1, 10, 10, 0);
    stream.commit_transaction();

    // 2. Start a "Ghost" Transaction and then ABORT
    stream.begin_transaction();
    for(int i = 0; i < 100; ++i) {
        stream.push_node(dgtl::ir::OpCode::ADD_I32, dgtl::ir::Flags::NONE, 500 + i, 1, 1, 0);
    }
    
    // Using the internal fixed abort instead of manual set_count
    stream.abort_transaction(); 

    // 3. Start a new "Clean" Transaction
    stream.begin_transaction();
    stream.push_node(dgtl::ir::OpCode::SYSCALL, dgtl::ir::Flags::NONE, 2, 1, 0, 0);
    stream.commit_transaction();

    size_t final_size = stream.size();
    std::cout << "[INFO] Stream Size after Ghost Abort: " << final_size << std::endl;

    // Verification
    if (final_size == 2) {
        auto node2 = stream.get_node(1);
        if (node2.opcode == dgtl::ir::OpCode::SYSCALL) {
            std::cout << "\033[1;32m[opt]\033[0m Transactional integrity verified. Ghost nodes purged." << std::endl;
        } else {
            std::cout << "\033[1;31m[FAIL]\033[0m Opcode mismatch at index 1. Got: " << (int)node2.opcode << std::endl;
        }
    } else {
        std::cout << "\033[1;31m[FAIL]\033[0m Stream leaked " << (final_size - 2) << " ghost nodes!" << std::endl;    
    }

    return 0;
}
