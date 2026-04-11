#include <iostream>
#include "dgtl/core/Arena.hpp"
#include "dgtl/ir/Global_IR.hpp"

int main() {
    std::cout << "--- [DGTL-TEST] 10_simd_512_alignment ---" << std::endl;
    dgtl::core::DGTLArena arena(1024); 
    dgtl::ir::IRStream stream(arena, 2048);

    stream.begin_transaction();
    
    // Test a SIMD Burst with Vector Port requirements
    stream.push_node(
        dgtl::ir::OpCode::GATHER_F32_512, 
        dgtl::ir::Flags::IS_SIMD_BURST | dgtl::ir::Flags::REQUIRES_VEC_PORT, 
        0x10, 0, 0, 0, 0
    );
    
    stream.commit_transaction();

    if (arena.get_checkpoint() >= 32) { // Ensure at least enough bytes for a wide node
        std::cout << "SUCCESS" << std::endl;
    } else {
        return 1;
    }
    return 0;
}
