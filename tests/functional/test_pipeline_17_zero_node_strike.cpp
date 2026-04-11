/* test_pipeline_17_zero_node_strike.cpp */
#include <iostream>
#include "dgtl/core/Arena.hpp"
#include "dgtl/ir/Global_IR.hpp"

int main() {
    std::cout << "--- [DGTL-TEST] 17_zero_node_strike ---" << std::endl;
    dgtl::core::DGTLArena arena(512);
    dgtl::ir::IRStream stream(arena, 1024);

    size_t before = arena.get_checkpoint();
    stream.begin_transaction();
    // No nodes pushed
    stream.commit_transaction();
    size_t after = arena.get_checkpoint();

    if (before == after) {
        std::cout << "SUCCESS" << std::endl;
    } else {
        std::cout << "FAIL: Arena moved despite zero nodes." << std::endl;
        return 1;
    }
    return 0;
}
