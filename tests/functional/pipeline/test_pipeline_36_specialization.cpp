#include <iostream>
#include "dgtl/ir/base/Global_IR.hpp"
#include "dgtl/core/Arena.hpp"

void run_test_pipeline_36_specialization() {
    std::cout << "--- [DGTL-TEST] 36_jit_specialization ---" << std::endl;
    dgtl::core::DGTL_Arena arena(512); 
    dgtl::ir::IRStream stream(arena, 1024);

    stream.begin_transaction();
    
    // Push a Guard Node: If this fails at runtime, it triggers a deoptimization
    stream.push_node(
        dgtl::ir::OpCode::GUARD_VALUE, 
        dgtl::ir::Flags::REQUIRES_DEPASS_TARGET, 
        0, 0, 0, 0, 0xBEEF // Intent Hash (the value we are guarding)
    );
    
    stream.commit_transaction();

    if (stream.size() > 0) {
        std::cout << "SUCCESS" << std::endl;
    } else {
        return;
    }
    return;
}

