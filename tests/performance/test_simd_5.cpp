#include <iostream>
#include <immintrin.h>
#include <chrono>
#include <vector>
#include <memory>
#include "dgtl/pipeline/Pass_Manager.hpp" 
#include "dgtl/ir/Global_IR.hpp"

using namespace dgtl;

/**
 * @brief SIMD Folding Strike for Bit-Packed IR.
 * Operates on op_a (result) and op_b (factor) using AVX2.
 */
class SIMDFoldingopt : public dgtl::pipeline::DGTLOptimizationPass {
public:
    void execute(ir::IRStream& stream) override {
        fold_stream(stream);
    }

    [[nodiscard]] const char* get_pass_name() const noexcept override {
        return "SIMD Folding Optimization";
    }

    void fold_stream(ir::IRStream& stream) {
        // IRNode is 16 bytes. Process 4 nodes (64 bytes) per iteration to hit the cache line.
        size_t i = 0;
        for (; i + 4 <= stream.count; i += 4) {
            // FLAG: BIT-PACKED LOAD
            // We load the 64-byte block (4 nodes). 
            // op_a is at offset 4, op_b at offset 8 within each 16-byte node.
            
            // Extract op_a (uint32_t) from 4 nodes into one __m128i, then convert to __m256d
            __m128i a_raw = _mm_set_epi32(stream.get_node_readonly(i+3).op_a, stream.get_node_readonly(i+2).op_a, 
                                          stream.get_node_readonly(i+1).op_a, stream.get_node_readonly(i).op_a);
            
            __m128i b_raw = _mm_set_epi32(stream.get_node_readonly(i+3).op_b, stream.get_node_readonly(i+2).op_b, 
                                          stream.get_node_readonly(i+1).op_b, stream.get_node_readonly(i).op_b);

            // Convert integer operands to double for high-precision folding
            __m256d va = _mm256_cvtepi32_pd(a_raw);
            __m256d vb = _mm256_cvtepi32_pd(b_raw);

            // THE STRIKE: Vectorized Multiplication
            __m256d res = _mm256_mul_pd(va, vb);

            // Convert back to i32 and store into op_a (the result field)
            __m128i res_i32 = _mm256_cvtpd_epi32(res);
            
            // Scalar-shuttle back to Bit-Packed fields (Final 1% bottleneck removal)
            auto& n0 = stream.get_node_ref(i);
            auto& n1 = stream.get_node_ref(i+1);
            auto& n2 = stream.get_node_ref(i+2);
            auto& n3 = stream.get_node_ref(i+3);
            n0.op_a = _mm_extract_epi32(res_i32, 0);
            n1.op_a = _mm_extract_epi32(res_i32, 1);
            n2.op_a = _mm_extract_epi32(res_i32, 2);
            n3.op_a = _mm_extract_epi32(res_i32, 3);
        }

        // Cleanup for non-multiple of 4
        for (; i < stream.size(); ++i) {
            auto& node = stream.get_node_ref(i);
            node.op_a *= node.op_b;
        }
    }
};

int main() {
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
    auto fold_opt = std::make_unique<SIMDFoldingopt>();
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
    return 0;
}
