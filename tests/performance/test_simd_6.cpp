#include <iostream>
#include <immintrin.h>
#include <chrono>
#include <malloc.h> // For _aligned_malloc
#include "Include/pipeline/Pass_Manager.hpp" 
#include "detail/ir_impl/BitPacked_IR.hpp"

using namespace dgtl;

class SIMDFoldingPass {
public:
    void fold_stream(ir::IRStream& stream) {
        size_t i = 0;
        // Process in blocks of 8 (128 bytes total - 2 cache lines)
        for (; i + 8 <= stream.count; i += 8) {
            __m256i a_vals = _mm256_set_epi32(
                stream.buffer[i+7].op_a, stream.buffer[i+6].op_a, stream.buffer[i+5].op_a, stream.buffer[i+4].op_a,
                stream.buffer[i+3].op_a, stream.buffer[i+2].op_a, stream.buffer[i+1].op_a, stream.buffer[i].op_a
            );
            
            __m256i b_vals = _mm256_set_epi32(
                stream.buffer[i+7].op_b, stream.buffer[i+6].op_b, stream.buffer[i+5].op_b, stream.buffer[i+4].op_b,
                stream.buffer[i+3].op_b, stream.buffer[i+2].op_b, stream.buffer[i+1].op_b, stream.buffer[i].op_b
            );

            // The Integer Strike
            __m256i res = _mm256_mullo_epi32(a_vals, b_vals);

            // Vectorized Extract-Write
            stream.buffer[i].op_a   = _mm256_extract_epi32(res, 0);
            stream.buffer[i+1].op_a = _mm256_extract_epi32(res, 1);
            stream.buffer[i+2].op_a = _mm256_extract_epi32(res, 2);
            stream.buffer[i+3].op_a = _mm256_extract_epi32(res, 3);
            stream.buffer[i+4].op_a = _mm256_extract_epi32(res, 4);
            stream.buffer[i+5].op_a = _mm256_extract_epi32(res, 5);
            stream.buffer[i+6].op_a = _mm256_extract_epi32(res, 6);
            stream.buffer[i+7].op_a = _mm256_extract_epi32(res, 7);
        }
        // Cleanup
        for (; i < stream.count; ++i) {
            stream.buffer[i].op_a *= stream.buffer[i].op_b;
        }
    }
};

int main(int argc, char** argv) {
    std::cout << "--- DGTL-X: INTEGER SIMD STRIKE (v2.0) ---" << std::endl;
    
    constexpr size_t NODE_COUNT = 1000000;
    void* raw_ptr = _aligned_malloc(NODE_COUNT * sizeof(ir::IRNode), 64);
    if (!raw_ptr) return -1;

    ir::IRNode* buffer = static_cast<ir::IRNode*>(raw_ptr);

    for(size_t i = 0; i < NODE_COUNT; ++i) {
        buffer[i].opcode = ir::OpCode::MUL_I32;
        buffer[i].op_a = static_cast<uint32_t>(i);
        buffer[i].op_b = 2;
    }

    ir::IRStream stream = { buffer, NODE_COUNT, NODE_COUNT };
    pipeline::DGTLPassManager manager;
    SIMDFoldingPass pass;
    manager.add_pass(pass);

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
    return 0;
}
