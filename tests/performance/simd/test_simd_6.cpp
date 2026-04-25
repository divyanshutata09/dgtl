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
#include <immintrin.h>
#include <chrono>
#include <malloc.h> // For _aligned_malloc
#include "dgtl/pipeline/Pass_Manager.hpp"
#include "dgtl/ir/Global_IR.hpp"
#include "dgtl/pipeline/Performance_Test_Probes.hpp"

using namespace dgtl;

void run_test_simd_6() {
    std::cout << "--- DGTL-X: INTEGER SIMD STRIKE (v2.0) ---" << std::endl;
    
    constexpr size_t NODE_COUNT = 1000000;
    void* raw_ptr = _aligned_malloc(NODE_COUNT * sizeof(ir::IRNode), 64);
    if (!raw_ptr) {
        std::cout << "[FAIL] Allocation failed." << std::endl;
        return;
    }

    ir::IRNode* buffer = static_cast<ir::IRNode*>(raw_ptr);

    for (size_t i = 0; i < NODE_COUNT; ++i) {
        buffer[i].opcode = ir::OpCode::MUL_I32;
        buffer[i].op_a = static_cast<uint32_t>(i);
        buffer[i].op_b = 2;
    }

    ir::IRStream stream(buffer, NODE_COUNT);
    pipeline::DGTLoptManager manager;
    manager.add_opt(std::make_unique<dgtl::pipeline::probes::SIMDIntMulFoldingPass>());

    auto start = std::chrono::high_resolution_clock::now();
    manager.execute(stream);
    auto end = std::chrono::high_resolution_clock::now();

    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

    if (buffer[500].op_a == 1000) {
        std::cout << "[SUCCESS] Integer SIMD Strike Verified." << std::endl;
        std::cout << "[PERF] Execution Time: " << duration << " us" << std::endl;
    } else {
        std::cout << "[FAIL] Data Corruption: " << buffer[500].op_a << std::endl;
    }

    _aligned_free(buffer);
    return;
}




