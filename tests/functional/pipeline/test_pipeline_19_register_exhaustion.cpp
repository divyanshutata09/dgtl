#include <iostream>
#include "dgtl/ir/base/Global_IR.hpp"
#include "dgtl/core/Arena.hpp"

void run_test_pipeline_19_register_exhaustion() {
    std::cout << "--- [DGTL-TEST] 19_register_exhaustion ---" << std::endl;
    dgtl::core::DGTL_Arena arena(1024 * 10); 
    size_t node_limit = 10;
    dgtl::ir::IRStream stream(arena, node_limit);

    // Push 20 nodes into a 10-node capacity stream
    for(int i = 0; i < 20; ++i) {
        stream.push_node(dgtl::ir::OpCode::NOP, 0, i, 0, 0, 0, 0);
    }

    if (stream.size() <= node_limit) {
        std::cout << "SUCCESS" << std::endl;
    } else {
        std::cout << "FAIL: Stream size (" << stream.size() << ") exceeded capacity (" << node_limit << ")" << std::endl;
        return;
    }
    return;
}

