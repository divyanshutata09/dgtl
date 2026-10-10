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

import dgtl;
import dgtl.arena;
import dgtl.ir.stream;
import dgtl.ir_enums;
import dgtl.ir_node;

#include <chrono>
#include <immintrin.h>
#include <iostream>
#include <malloc.h> // For _aligned_malloc
import dgtl.ir.pipeline.performance_probes;

void run_test_simd_6() {
    constexpr size_t kNodeCount = 1000000;

    std::cout << "--- DGTL-X: INTEGER SIMD STRIKE (v1.0.0) ---\n";
    
    void* raw_ptr = _aligned_malloc(kNodeCount * sizeof(dgtl::ir::IRNode), 64);
    if (!raw_ptr) {
        std::cout << "[FAIL] Allocation failed.\n";
        return;
    }

    auto* buffer = static_cast<dgtl::ir::IRNode*>(raw_ptr);

    for (size_t i = 0; i < kNodeCount; ++i) {
        buffer[i].opcode = dgtl::ir::OpCode::MUL_I32;
        buffer[i].op_a = static_cast<uint32_t>(i);
        buffer[i].op_b = 2;
    }

    dgtl::ir::IRStream stream(buffer, kNodeCount);
    dgtl::pipeline::DGTLoptManager manager;
    manager.add_opt(std::make_unique<dgtl::pipeline::probes::SIMDIntMulFoldingPass>());

    auto start = std::chrono::high_resolution_clock::now();
    manager.execute(stream);
    auto end = std::chrono::high_resolution_clock::now();

    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

    if (buffer[500].op_a == 1000) {
        std::cout << "[SUCCESS] Integer SIMD Strike Verified.\n";
        std::cout << "[PERF] Execution Time: " << duration << " us\n";
    } else {
        std::cout << "[FAIL] Data Corruption: " << buffer[500].op_a << "\n";
    }

    _aligned_free(buffer);
    return;
}







