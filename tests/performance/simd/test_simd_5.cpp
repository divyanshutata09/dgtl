#include <iostream>
#include <immintrin.h>
#include <chrono>
#include <vector>
#include <memory>
#include "dgtl/pipeline/Pass_Manager.hpp" 
#include "dgtl/ir/base/Global_IR.hpp"
#include "dgtl/pipeline/Performance_Test_Probes.hpp"

using namespace dgtl;

void run_test_simd_5() {
    std::cout << "--- DGTL-X: BIT-PACKED SIMD STRIKE (V1.0) ---" << std::endl;
    
    constexpr size_t NODE_COUNT = 1000000;
    // Aligning to 64 bytes for the optManager's Prefetcher
    // FLAG FIXED: Using _aligned_malloc for Windows/MinGW stability
    void* raw_ptr = _aligned_malloc(NODE_COUNT * sizeof(ir::IRNode), 64);
    ir::IRNode* buffer = static_cast<ir::IRNode*>(raw_ptr);

    // Initialize Bit-Packed Nodes
    for(size_t i = 0; i < NODE_COUNT; ++i) {
        buffer[i].opcode = ir::OpCode::MUL_I32;
        buffer[i].flags = ir::Flags::IS_SIMD_BURST;
        buffer[i].op_a = static_cast<uint32_t>(i);
        buffer[i].op_b = 2; // Factor of 2
    }

    ir::IRStream global_stream(buffer, NODE_COUNT);

    dgtl::pipeline::DGTLoptManager manager;
    auto fold_opt = std::make_unique<dgtl::pipeline::probes::SIMDIntMulFoldingPass>();
    manager.add_opt(std::move(fold_opt));

    auto start = std::chrono::high_resolution_clock::now();
    manager.execute(global_stream, dgtl::pipeline::TargetPrecision::HFT);
    auto end = std::chrono::high_resolution_clock::now();

    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

    // Integrity Check on Bit-Packed Data
    if (buffer[500].op_a == 1000) {
        std::cout << "[SUCCESS] Bit-Packed SIMD Strike Verified." << std::endl;
        std::cout << "[PERF] Execution Time: " << duration << " us" << std::endl;
    } else {
        std::cout << "[FAIL] Data Corruption in op_a. Value: " << buffer[500].op_a << std::endl;
    }

    _aligned_free(buffer);
    return;
}

