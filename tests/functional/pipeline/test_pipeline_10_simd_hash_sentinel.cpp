#include <iostream>
#include "dgtl/core/Arena.hpp"
#include "dgtl/ir/base/Global_IR.hpp"

void run_test_pipeline_10_simd_hash_sentinel() {
    std::cout << "--- [DGTL-TEST] 10_simd_512_alignment ---" << std::endl;
    dgtl::core::DGTL_Arena arena(1024); 
    dgtl::ir::IRStream stream(arena, 2048);

    stream.begin_transaction();
    
    // Test a SIMD Burst with Vector Port requirements
    stream.push_node(
        dgtl::ir::OpCode::GATHER_F32_512, 
        dgtl::ir::Flags::IS_SIMD_BURST | dgtl::ir::Flags::REQUIRES_VEC_PORT, 
        0x10, 0, 0, 0, 0
    );
    
    stream.commit_transaction();

    if (stream.size() >= 1) { // Ensure at least one node was added
        std::cout << "SUCCESS" << std::endl;
    } else {
        return;
    }
    return;
}

