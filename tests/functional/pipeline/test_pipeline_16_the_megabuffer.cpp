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

using namespace dgtl;

void run_test_pipeline_16_the_megabuffer() {
    std::cout << "--- [DGTL-TEST] 16_the_megabuffer ---" << std::endl;
    constexpr size_t kNodes = 4096;
    core::DGTL_Arena arena(128);
    ir::IRStream stream(arena, kNodes + 32);

    stream.begin_transaction();
    for (size_t i = 0; i < kNodes; ++i) {
        const uint32_t rid = static_cast<uint32_t>(i + 1);
        const uint32_t a = (i == 0) ? 1U : static_cast<uint32_t>(i);
        stream.push_node(ir::OpCode::ADD_I32, ir::Flags::NONE, rid, a, 1, 0, 0);
    }
    stream.push_node(ir::OpCode::RET, ir::Flags::IS_RETURN, 0, static_cast<uint32_t>(kNodes), 0, 0, 0);
    stream.commit_transaction();

    const size_t before = stream.size();

    dgtl::pass::DGTLGVN gvn;
    gvn.execute(stream);
    stream.perform_dead_code_elimination();

    const bool reduced_or_same = stream.size() <= before;
    const bool non_empty = stream.size() > 0;
    const bool valid = stream.validate_shadow_integrity();

    if (reduced_or_same && non_empty && valid) {
        std::cout << "SUCCESS" << std::endl;
        return;
    }

    std::cout << "FAIL" << std::endl;
    return;
}








