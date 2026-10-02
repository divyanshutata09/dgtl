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
#include <atomic>
#include <cstdint>
#include <cmath>

#define NOINLINE __attribute__((noinline))

// Prevent compiler from optimizing away values
volatile int sink_i;
volatile float sink_f;

// -----------------------------
// TEST 1: Alias Analysis
// -----------------------------
NOINLINE void test_alias(float* a, float* b, float* out) {
    a[0] = 10.0f;
    float x = b[0];
    a[0] = 20.0f;
    *out = x;
}

// -----------------------------
// TEST 2: Store Forwarding
// -----------------------------
NOINLINE float test_store_forward(float* a) {
    a[0] = 5.0f;
    return a[0];
}

// -----------------------------
// TEST 3: Interprocedural (IPO)
// -----------------------------
NOINLINE int add_func(int a, int b) {
    return a + b;
}

NOINLINE int test_ipo() {
    return add_func(10, 20);
}

// -----------------------------
// TEST 4: Control Flow Chaos
// -----------------------------
NOINLINE int test_cfg(int x) {
    if (x > 0) {
        if (x > 10) return 1;
    } else {
        if (x < -10) return 2;
    }
    return 0;
}

// -----------------------------
// TEST 5: Loop Dependence
// -----------------------------
NOINLINE void test_loop_dep(int* A, int N) {
    for (int i = 1; i < N; i++) {
        A[i] = A[i-1] + 1;
    }
}

NOINLINE void test_loop_indep(int* A, int* B, int N) {
    for (int i = 0; i < N; i++) {
        B[i] = A[i] + 1;
    }
}

// -----------------------------
// TEST 6: Register Pressure
// -----------------------------
NOINLINE int test_regs(
    int a,int b,int c,int d,int e,int f,int g,int h,int i,int j,
    int k,int l,int m,int n,int o,int p,int q,int r,int s,int t)
{
    return a+b+c+d+e+f+g+h+i+j+k+l+m+n+o+p+q+r+s+t;
}

// -----------------------------
// TEST 7: Volatile Correctness
// -----------------------------
volatile int vol_x;

NOINLINE int test_volatile() {
    vol_x = 10;
    vol_x = 20;
    return vol_x;
}

// -----------------------------
// TEST 8: Vectorization
// -----------------------------
NOINLINE void test_vector(float* A, float* B, float* C, int N) {
    for (int i = 0; i < N; i++) {
        A[i] = B[i] + C[i];
    }
}

// Strided (hard case)
NOINLINE void test_vector_stride(float* A, float* B, float* C, int N, int stride) {
    for (int i = 0; i < N; i++) {
        A[i] = B[i] + C[i * stride];
    }
}

// -----------------------------
// TEST 9: UB Edge (no crash, just behavior)
// -----------------------------
NOINLINE int test_ub(int x) {
    if (x + 1 < x) return 1;
    return 0;
}

// -----------------------------
// TEST 10: Atomics / Memory Order
// -----------------------------
std::atomic<int> atomic_x(0);

NOINLINE void writer() {
    atomic_x.store(1, std::memory_order_release);
}

NOINLINE int reader() {
    return atomic_x.load(std::memory_order_acquire);
}

// -----------------------------
// MAIN DRIVER
// -----------------------------
int main() {
    std::cout << "=== DGTL REALITY AUDIT START ===\n";

    // Alias test
    float a = 1.0f, b = 2.0f, out = 0.0f;
    test_alias(&a, &b, &out);
    std::cout << "[Alias] out=" << out << "\n";

    // Store forward
    float arr = 0.0f;
    float sf = test_store_forward(&arr);
    std::cout << "[StoreForward] " << sf << "\n";

    // IPO
    std::cout << "[IPO] " << test_ipo() << "\n";

    // CFG
    std::cout << "[CFG] " << test_cfg(20) << "\n";

    // Loop tests
    const int N = 64;
    int A[N], B[N];
    for (int i = 0; i < N; i++) A[i] = i;

    test_loop_dep(A, N);
    test_loop_indep(A, B, N);
    std::cout << "[Loop] B[10]=" << B[10] << "\n";

    // Register pressure
    int reg = test_regs(1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1);
    std::cout << "[Regs] " << reg << "\n";

    // Volatile
    std::cout << "[Volatile] " << test_volatile() << "\n";

    // Vector
    float VA[N], VB[N], VC[N];
    for (int i = 0; i < N; i++) {
        VB[i] = i;
        VC[i] = i * 2;
    }

    test_vector(VA, VB, VC, N);
    test_vector_stride(VA, VB, VC, N, 2);
    std::cout << "[Vector] VA[10]=" << VA[10] << "\n";

    // UB
    std::cout << "[UB] " << test_ub(10) << "\n";

    // Atomics
    writer();
    std::cout << "[Atomic] " << reader() << "\n";

    std::cout << "=== AUDIT END ===\n";
    return 0;
}
