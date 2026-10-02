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

#define NOINLINE __attribute__((noinline))

volatile int sink;

// =====================================================
// 1. ALIAS + FLOW SENSITIVITY STRESS
// =====================================================
NOINLINE float alias_flow(float* a, float* b, int c) {
    float* p = a;

    if (c & 1) p = b;
    if (c & 2) a[0] = 10.0f;
    if (c & 4) b[0] = 20.0f;

    float x = p[0];

    if (c & 8) a[0] = 30.0f;

    return x;
}

// =====================================================
// 2. PARTIAL ALIAS + MEMORY OVERLAP
// =====================================================
NOINLINE void overlap(float* a) {
    float* b = a + 1;
    a[0] = 1.0f;
    b[0] = 2.0f;
    sink = (int)a[1];
}

// =====================================================
// 3. INDIRECT CALL + IPO LIMIT TEST
// =====================================================
NOINLINE int add(int a, int b) { return a + b; }
NOINLINE int mul(int a, int b) { return a * b; }

NOINLINE int dispatcher(int (*fn)(int,int), int x) {
    return fn(x + 1, x + 2);
}

// =====================================================
// 4. LOOP DEPENDENCY (CHAINED)
// =====================================================
NOINLINE void loop_chain(float* A, int N) {
    for (int i = 1; i < N; i++) {
        A[i] = A[i-1] * 0.99f + 1.0f;
    }
}

// =====================================================
// 5. VECTORIZATION TRAP (MOD + NONLINEAR)
// =====================================================
NOINLINE void loop_vector_trap(float* A, float* B, int N) {
    for (int i = 0; i < N; i++) {
        A[i] = B[i] + B[(i * 3 + 7) % N];
    }
}

// =====================================================
// 6. STRIDED GATHER / SCATTER
// =====================================================
NOINLINE void loop_stride(float* A, float* B, int N, int stride) {
    for (int i = 0; i < N; i++) {
        A[i] = B[i * stride] * 2.0f;
    }
}

// =====================================================
// 7. REDUCTION (VECTOR TARGET)
// =====================================================
NOINLINE float reduction(float* A, int N) {
    float sum = 0;
    for (int i = 0; i < N; i++) {
        sum += A[i];
    }
    return sum;
}

// =====================================================
// 8. REGISTER PRESSURE + LIVE RANGE INTERFERENCE
// =====================================================
NOINLINE int reg_pressure(int x) {
    int a=x+1,b=x+2,c=x+3,d=x+4,e=x+5,f=x+6,g=x+7,h=x+8,i=x+9,j=x+10;
    int k=x+11,l=x+12,m=x+13,n=x+14,o=x+15,p=x+16,q=x+17,r=x+18,s=x+19,t=x+20;

    return a+b+c+d+e+f+g+h+i+j+k+l+m+n+o+p+q+r+s+t;
}

// =====================================================
// 9. CFG COMPLEXITY (DEAD + LIVE MIX)
// =====================================================
NOINLINE int cfg(int x) {
    if (x > 0) {
        if (x < 0) return 1;
        if (x == 100) return 2;
        if (x > 50) return 3;
    }
    return 4;
}

// =====================================================
// 10. VOLATILE ORDERING
// =====================================================
volatile int v;

NOINLINE int vol_test() {
    v = 1;
    int a = v;
    v = 2;
    return a;
}

// =====================================================
// 11. ATOMIC ORDER + LOOP
// =====================================================
std::atomic<int> ax(0);

NOINLINE int atomic_test() {
    for (int i = 0; i < 5; i++) {
        ax.store(i, std::memory_order_release);
    }
    return ax.load(std::memory_order_acquire);
}

// =====================================================
// 12. UB EDGE CASE
// =====================================================
NOINLINE int ub(int x) {
    return (x + 1 < x);
}

// =====================================================
// MAIN DRIVER
// =====================================================
int main() {
    std::cout << "=== DGTL FULL COMPILER STRESS TEST ===\n";

    const int N = 128;
    float A[N], B[N];

    for (int i = 0; i < N; i++) {
        A[i] = i;
        B[i] = i * 2;
    }

    std::cout << "[AliasFlow] " << alias_flow(A, B, 7) << "\n";

    overlap(A);

    std::cout << "[Dispatch-add] " << dispatcher(add, 3) << "\n";
    std::cout << "[Dispatch-mul] " << dispatcher(mul, 3) << "\n";

    loop_chain(A, N);
    loop_vector_trap(A, B, N);
    loop_stride(A, B, N, 2);

    std::cout << "[Reduction] " << reduction(A, N) << "\n";
    std::cout << "[Reg] " << reg_pressure(1) << "\n";
    std::cout << "[CFG] " << cfg(60) << "\n";
    std::cout << "[Volatile] " << vol_test() << "\n";
    std::cout << "[Atomic] " << atomic_test() << "\n";
    std::cout << "[UB] " << ub(10) << "\n";

    std::cout << "=== END TEST ===\n";
}