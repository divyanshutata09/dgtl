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

volatile int sink_i;
volatile float sink_f;

// -----------------------------
// 1. Alias + Control Fusion
// -----------------------------
NOINLINE float alias_cfg_mix(float* a, float* b, int cond) {
    a[0] = 10.0f;

    if (cond & 1) b = a;      // dynamic alias
    if (cond & 2) a[0] = 30;  // overwrite before read

    float x = b[0];

    if (cond & 4) a[0] = 50;

    return x;
}

// -----------------------------
// 2. Indirect IPO + Folding
// -----------------------------
NOINLINE int add(int a, int b) { return a + b; }

NOINLINE int call_indirect(int (*fn)(int,int), int x) {
    if (x > 0) return fn(10, 20);
    else return fn(1, 2);
}

// -----------------------------
// 3. Loop: Mixed Dependencies
// -----------------------------
NOINLINE void loop_mixed(float* A, float* B, int N) {
    for (int i = 1; i < N; i++) {
        A[i] = A[i-1] + B[i] * 2.0f;
    }
}

// -----------------------------
// 4. Loop: Vector + Alias Trap
// -----------------------------
NOINLINE void loop_vector_alias(float* A, float* B, int N) {
    for (int i = 0; i < N; i++) {
        A[i] = B[i] + B[(i+1) % N]; // wrap dependency
    }
}

// -----------------------------
// 5. Loop: Stride + Nonlinear
// -----------------------------
NOINLINE void loop_stride(float* A, float* B, int N, int stride) {
    for (int i = 0; i < N; i++) {
        A[i] = B[i * stride] + 1.0f;
    }
}

// -----------------------------
// 6. Register Pressure + Branch
// -----------------------------
NOINLINE int reg_pressure(int x) {
    int a=x+1,b=x+2,c=x+3,d=x+4,e=x+5,f=x+6,g=x+7,h=x+8,i=x+9,j=x+10;
    int k=x+11,l=x+12,m=x+13,n=x+14,o=x+15,p=x+16,q=x+17,r=x+18,s=x+19,t=x+20;

    if (x > 5)
        return a+b+c+d+e+f+g+h+i+j;
    else
        return k+l+m+n+o+p+q+r+s+t;
}

// -----------------------------
// 7. CFG Deep Folding
// -----------------------------
NOINLINE int cfg_fold() {
    int x = 100;
    if (x > 0) {
        if (x > 50) {
            if (x > 75) return 1;
        }
    }
    return 0;
}

// -----------------------------
// 8. Volatile + Reordering Trap
// -----------------------------
volatile int vol;

NOINLINE int volatile_mix() {
    vol = 1;
    int x = vol;
    vol = 2;
    return x;
}

// -----------------------------
// 9. Atomics Ordering
// -----------------------------
std::atomic<int> ax(0);

NOINLINE void writer() {
    ax.store(99, std::memory_order_release);
}

NOINLINE int reader() {
    return ax.load(std::memory_order_acquire);
}

// -----------------------------
// 10. UB Edge Case
// -----------------------------
NOINLINE int ub_test(int x) {
    if (x + 1 < x) return 1;
    return 0;
}

// -----------------------------
// MAIN
// -----------------------------
int main() {
    std::cout << "=== DGTL FINAL BOSS TEST ===\n";

    const int N = 128;
    float A[N], B[N];

    for (int i = 0; i < N; i++) {
        A[i] = i;
        B[i] = i * 2;
    }

    std::cout << "[AliasCFG] " << alias_cfg_mix(A, B, 3) << "\n";
    std::cout << "[IPO] " << call_indirect(add, 1) << "\n";

    loop_mixed(A, B, N);
    loop_vector_alias(A, B, N);
    loop_stride(A, B, N, 2);

    std::cout << "[Reg] " << reg_pressure(3) << "\n";
    std::cout << "[CFG] " << cfg_fold() << "\n";
    std::cout << "[Volatile] " << volatile_mix() << "\n";

    writer();
    std::cout << "[Atomic] " << reader() << "\n";

    std::cout << "[UB] " << ub_test(10) << "\n";

    std::cout << "=== END ===\n";
}
