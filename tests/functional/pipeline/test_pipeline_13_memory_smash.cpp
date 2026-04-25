/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:    DGTL (Deep General-purpose Transformation Layer)
 * Owner:      Divyanshu Tata
 * Repository: github.com/TataG-Systems/DGTL
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

#include <iostream>
#include "dgtl/ir/Global_IR.hpp"
#include "dgtl/pass/transform/GVN.hpp"
#include "dgtl/pass/simd/SIMD_Legalizer.hpp"
#include "dgtl/isa/NASM_Emitter.hpp"
#include "dgtl/core/Arena.hpp"
#include "dgtl/core/Verified_Formal.hpp"
#include "dgtl/analysis/cost/AI_Cost_Model.hpp"
#include "dgtl/analysis/Memory_Dependency_Smasher.hpp"


using namespace dgtl;

void run_test_pipeline_13_memory_smash() {
    std::cout << "--- [DGTL-TEST] 13_memory_smash ---" << std::endl;
    core::DGTL_Arena arena(64);
    ir::IRStream stream(arena, 64);

    dgtl::core::DGTLFormalVerifier verifier;
    dgtl::analysis::DGTLAICostModel model;
    dgtl::analysis::DGTLMemorySmasher smasher(verifier, model);

    smasher.register_ptr_region(100, 1);
    smasher.register_ptr_region(200, 2);

    stream.begin_transaction();
    stream.push_node(ir::OpCode::STORE, ir::Flags::NONE, 1, 100, 0, 0, 0);
    stream.push_node(ir::OpCode::LOAD, ir::Flags::NONE, 2, 200, 0, 0, 0);
    stream.commit_transaction();

    const bool reorder_ok = smasher.can_reorder(stream, 0, 1);
    if (reorder_ok) {
        std::cout << "SUCCESS" << std::endl;
        return;
    }

    std::cout << "FAIL" << std::endl;
    return;
}








