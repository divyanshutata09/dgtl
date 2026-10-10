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
#include <memory>
import dgtl.ir.pipeline.performance_probes;

void run_test_simd_5() {
    constexpr size_t kNodeCount = 1000000;

    std::cout << "--- DGTL-X: BIT-PACKED SIMD STRIKE (v1.0.0) ---\n";
    
    // Aligning to 64 bytes for the optManager's Prefetcher
    // FLAG FIXED: Using _aligned_malloc for Windows/MinGW stability
    void* raw_ptr = _aligned_malloc(kNodeCount * sizeof(dgtl::ir::IRNode), 64);
    auto* buffer = static_cast<dgtl::ir::IRNode*>(raw_ptr);
    if (buffer == nullptr) {
        std::cout << "[FAIL] _aligned_malloc failed for SIMD buffer allocation.\n";
        return;
    }

    // Initialize Bit-Packed Nodes
    for (size_t i = 0; i < kNodeCount; ++i) {
        buffer[i].opcode = dgtl::ir::OpCode::MUL_I32;
        buffer[i].flags = dgtl::ir::Flags::IS_SIMD_BURST;
        buffer[i].op_a = static_cast<uint32_t>(i);
        buffer[i].op_b = 2; // Factor of 2
    }

    dgtl::ir::IRStream global_stream(buffer, kNodeCount);

    dgtl::pipeline::DGTLoptManager manager;
    auto fold_opt = std::make_unique<dgtl::pipeline::probes::SIMDIntMulFoldingPass>();
    manager.add_opt(std::move(fold_opt));

    auto start = std::chrono::high_resolution_clock::now();
    manager.execute(global_stream, dgtl::pipeline::TargetPrecision::HFT);
    auto end = std::chrono::high_resolution_clock::now();

    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

    // Integrity Check on Bit-Packed Data
    if (buffer[500].op_a == 1000) {
        std::cout << "[SUCCESS] Bit-Packed SIMD Strike Verified.\n";
        std::cout << "[PERF] Execution Time: " << duration << " us\n";
    } else {
        std::cout << "[FAIL] Data Corruption in op_a. Value: " << buffer[500].op_a << "\n";
    }

    _aligned_free(buffer);
    return;
}







