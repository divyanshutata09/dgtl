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
#include <immintrin.h> // Critical for AVX/SIMD intrinsics
#include <vector>
#include <chrono>

void dgtl_matmul_avx(const float* A, const float* B, float* C, int N) {
    // Simplified AVX-256 block for demonstration (can be extended to AVX-512)
    for (int i = 0; i < N; ++i) {
        for (int j = 0; j < N; j += 8) { // Process 8 floats at once
            __m256 c_vec = _mm256_loadu_ps(&C[i * N + j]);
            for (int k = 0; k < N; ++k) {
                __m256 a_vec = _mm256_set1_ps(A[i * N + k]);
                __m256 b_vec = _mm256_loadu_ps(&B[k * N + j]);
                // Fused Multiply-Add: C = A * B + C
                c_vec = _mm256_fmadd_ps(a_vec, b_vec, c_vec); 
            }
            _mm256_storeu_ps(&C[i * N + j], c_vec);
        }
    }
}

int main() {
    std::cout << "[TEST] Validating Vectorized Math Engine..." << std::endl;
    int N = 256; 
    std::vector<float> A(N * N, 1.0f);
    std::vector<float> B(N * N, 2.0f);
    std::vector<float> C(N * N, 0.0f);

    auto start = std::chrono::high_resolution_clock::now();
    dgtl_matmul_avx(A.data(), B.data(), C.data(), N);
    auto end = std::chrono::high_resolution_clock::now();

    std::cout << "[PASS] Hardware FMA Matrix Math executed in: " 
              << std::chrono::duration_cast<std::chrono::microseconds>(end - start).count() 
              << " ms." << std::endl;
    return 0;
}