#include <iostream>
#include <cassert>
#include "dgtl/ir/base/Global_IR.hpp"
#include "dgtl/core/Arena.hpp"
#include "dgtl/analysis/dataflow/Liveness_Analyzer.hpp"
#include "dgtl/isa/regalloc/ARM64_RegAlloc.hpp"

using namespace dgtl;

void run_test_pipeline_21_dual_bank() {
    core::DGTL_Arena a(2);
    ir::IRStream s(a, 16);
    
    // Using the actual framework class instead of a mock
    analysis::LivenessAnalyzer liveness; 
    dgtl::isa::DGTLARM64RegAlloc allocator;

    s.begin_transaction();
    
    // 1. Scalar Op (Should get GPR, e.g., X0)
    s.push_node(ir::OpCode::ADD_I32, ir::Flags::NONE, 10, 1, 2, 0);
    
    // 2. SIMD Op (Should get NEON, e.g., V0)
    // Manually setting the flag that the Legalizer would set
    s.push_node(static_cast<ir::OpCode>(0x304), ir::Flags::IS_SIMD_BURST, 20, 11, 12, 0);
    
    // Note: If your real liveness.is_live_at() defaults to true, this works perfectly.
    allocator.execute(s, liveness);

    auto scalar_node = s.get_node(0);
    auto vector_node = s.get_node(1);

    // op_c stores the physical register index after execute()
    bool scalar_ok = (scalar_node.op_c < 31);
    bool vector_ok = (vector_node.op_c < 32); 

    std::cout << "Scalar Phys Reg: X" << (int)scalar_node.op_c << std::endl;
    std::cout << "Vector Phys Reg: V" << (int)vector_node.op_c << std::endl;

    if(scalar_ok && vector_ok) std::cout << "SUCCESS";

    return;
}



