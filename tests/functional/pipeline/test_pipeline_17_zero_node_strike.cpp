/* test_pipeline_17_zero_node_strike.cpp */
#include <iostream>
#include "dgtl/core/Arena.hpp"
#include "dgtl/ir/base/Global_IR.hpp"

void run_test_pipeline_17_zero_node_strike() {
    std::cout << "--- [DGTL-TEST] 17_zero_node_strike ---" << std::endl;
    dgtl::core::DGTL_Arena arena(512);
    dgtl::ir::IRStream stream(arena, 1024);

    size_t before = stream.size();
    stream.begin_transaction();
    // No nodes pushed
    stream.commit_transaction();
    size_t after = stream.size();

    if (before == after) {
        std::cout << "SUCCESS" << std::endl;
    } else {
        std::cout << "FAIL: Stream size changed despite zero nodes." << std::endl;
        return;
    }
    return;
}

