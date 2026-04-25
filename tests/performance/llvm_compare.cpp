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
#include <vector>
#include <chrono>
#include <immintrin.h>

using Clock = std::chrono::high_resolution_clock;

// prevent over-optimization
volatile double sink_d;
volatile int sink_i;

// -----------------------------
// TEST 1: Dependency Chain
// -----------------------------
double test_dependency_chain() {
    const int N = 100000000;
    std::vector<int> a(N, 1), b(N, 2), c(N, 0);

    auto start = Clock::now();

    for (int i = 1; i < N; i++) {
        c[i] = a[i] + b[i] + c[i - 1];
    }

    sink_i = c[N - 1];

    auto end = Clock::now();
    return std::chrono::duration<double>(end - start).count();
}

// -----------------------------
// TEST 2: ECS / Memory Layout
// -----------------------------
struct Entity {
    float x, y, z;
    float vx, vy, vz;
};

double test_ecs() {
    const int N = 5'000'000;
    std::vector<Entity> e(N);

    for (int i = 0; i < N; i++) {
        e[i] = {1,2,3,0.1f,0.1f,0.1f};
    }

    auto start = Clock::now();

    for (int i = 0; i < N; i++) {
        e[i].x += e[i].vx;
        e[i].y += e[i].vy;
        e[i].z += e[i].vz;
    }

    sink_d = e[0].x;

    auto end = Clock::now();
    return std::chrono::duration<double>(end - start).count();
}

// -----------------------------
// TEST 3: SIMD Kernel
// -----------------------------
double test_simd() {
    const int N = 1 << 24;

    alignas(32) float *a = new float[N];
    alignas(32) float *b = new float[N];
    alignas(32) float *c = new float[N];

    for (int i = 0; i < N; i++) {
        a[i] = i * 1.0f;
        b[i] = i * 2.0f;
    }

    auto start = Clock::now();

    for (int i = 0; i < N; i += 8) {
        __m256 va = _mm256_load_ps(&a[i]);
        __m256 vb = _mm256_load_ps(&b[i]);
        __m256 vc = _mm256_add_ps(va, vb);
        _mm256_store_ps(&c[i], vc);
    }

    sink_d = c[0];

    auto end = Clock::now();

    delete[] a;
    delete[] b;
    delete[] c;

    return std::chrono::duration<double>(end - start).count();
}

// -----------------------------
// TEST 4: Branch-heavy HFT
// -----------------------------
double test_hft() {
    volatile double price = 100.0;
    volatile double result = 0;

    auto start = Clock::now();

    for (int i = 0; i < 150000000; i++) {
        if (price > 100.5)
            result += price * 0.1;
        else
            result -= price * 0.1;

        price += (i % 3 == 0 ? 0.01 : -0.01);
    }

    sink_d = result;

    auto end = Clock::now();
    return std::chrono::duration<double>(end - start).count();
}

// -----------------------------
// MAIN RUNNER
// -----------------------------
int main() {
    std::cout << "==============================\n";
    std::cout << " DGTL vs LLVM BENCH SUITE\n";
    std::cout << "==============================\n\n";

    std::cout << "[TEST 1] Dependency Chain: " 
              << test_dependency_chain() << " sec\n";

    std::cout << "[TEST 2] ECS Update: " 
              << test_ecs() << " sec\n";

    std::cout << "[TEST 3] SIMD Kernel: " 
              << test_simd() << " sec\n";

    std::cout << "[TEST 4] HFT Branching: " 
              << test_hft() << " sec\n";

    std::cout << "\nDONE.\n";
}
