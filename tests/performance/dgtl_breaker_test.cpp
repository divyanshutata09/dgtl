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

// =====================================================
// 1. ALIAS + PHI NODE CONFUSION
// =====================================================
NOINLINE float alias_phi(float* a, float* b, int cond) {
    float* p = cond ? a : b;

    a[0] = 10.0f;
    float x = p[0];   // may alias or not
    a[0] = 20.0f;

    return x;
}

// =====================================================
// 2. PARTIAL OVERLAP (HARD ALIAS)
// =====================================================
NOINLINE void partial_overlap(float* a) {
    float* b = a + 1;

    a[0] = 1.0f;
    b[0] = 2.0f;   // overlaps with a[1]

    sink_f = a[1];
}

// =====================================================
// 3. INDIRECT IPO + MULTI PATH
// =====================================================
NOINLINE int add(int a, int b) { return a + b; }
NOINLINE int sub(int a, int b) { return a - b; }

NOINLINE int dispatch(int (*fn)(int,int), int x) {
    if (x > 0) return fn(10, 20);
    else return fn(50, 5);
}

// =====================================================
// 4. LOOP CARRIED DEPENDENCY (REAL BLOCKER)
// =====================================================
NOINLINE void loop_dep(float* A, int N) {
    for (int i = 1; i < N; i++) {
        A[i] = A[i-1] * 1.1f;
    }
}

// =====================================================
// 5. FAKE VECTOR FRIENDLY BUT BROKEN
// =====================================================
NOINLINE void loop_fake_vector(float* A, float* B, int N) {
    for (int i = 0; i < N; i++) {
        A[i] = B[i] + B[(i + (i % 3))]; // non-linear
    }
}

// =====================================================
// 6. STRIDED GATHER (ADVANCED)
// =====================================================
NOINLINE void loop_gather(float* A, float* B, int N, int stride) {
    for (int i = 0; i < N; i++) {
        A[i] = B[i * stride];
    }
}

// =====================================================
// 7. LOOP WITH UNKNOWN BOUNDS
// =====================================================
NOINLINE int loop_unknown(int* A, int N) {
    int sum = 0;
    for (int i = 0; i < N; i++) {
        sum += A[i];
    }
    return sum;
}

// =====================================================
// 8. REGISTER PRESSURE + LIVE RANGE OVERLAP
// =====================================================
NOINLINE int reg_pressure_heavy(int x) {
    int a=x+1,b=x+2,c=x+3,d=x+4,e=x+5,f=x+6,g=x+7,h=x+8,i=x+9,j=x+10;
    int k=x+11,l=x+12,m=x+13,n=x+14,o=x+15,p=x+16,q=x+17,r=x+18,s=x+19,t=x+20;

    int u = a+b+c+d+e;
    int v = f+g+h+i+j;
    int w = k+l+m+n+o;
    int z = p+q+r+s+t;

    return u + v + w + z;
}

// =====================================================
// 9. CFG + DEAD PATH ELIMINATION
// =====================================================
NOINLINE int cfg_complex() {
    int x = 42;

    if (x > 0) {
        if (x < 0) return 1; // impossible
        if (x == 42) return 2;
    }

    return 3;
}

// =====================================================
// 10. VOLATILE ORDER TRAP
// =====================================================
volatile int vol;

NOINLINE int volatile_order() {
    vol = 5;
    int x = vol;
    vol = 10;
    return x;
}

// =====================================================
// 11. ATOMIC CONTENTION MODEL
// =====================================================
std::atomic<int> ax(0);

NOINLINE void writer() {
    for (int i = 0; i < 10; i++) {
        ax.store(i, std::memory_order_release);
    }
}

NOINLINE int reader() {
    int val = 0;
    for (int i = 0; i < 10; i++) {
        val = ax.load(std::memory_order_acquire);
    }
    return val;
}

// =====================================================
// 12. UB EDGE (OPT BAIT)
// =====================================================
NOINLINE int ub_edge(int x) {
    return (x + 1 < x);
}

// =====================================================
// MAIN DRIVER
// =====================================================
int main() {
    std::cout << "=== DGTL BREAKER TEST ===\n";

    const int N = 128;
    float A[N], B[N];
    int IA[N];

    for (int i = 0; i < N; i++) {
        A[i] = i;
        B[i] = i * 2;
        IA[i] = i;
    }

    std::cout << "[AliasPhi] " << alias_phi(A, B, 1) << "\n";

    partial_overlap(A);

    std::cout << "[IPO-add] " << dispatch(add, 1) << "\n";
    std::cout << "[IPO-sub] " << dispatch(sub, -1) << "\n";

    loop_dep(A, N);
    loop_fake_vector(A, B, N);
    loop_gather(A, B, N, 2);

    std::cout << "[LoopSum] " << loop_unknown(IA, N) << "\n";

    std::cout << "[RegHeavy] " << reg_pressure_heavy(1) << "\n";

    std::cout << "[CFG] " << cfg_complex() << "\n";

    std::cout << "[Volatile] " << volatile_order() << "\n";

    writer();
    std::cout << "[Atomic] " << reader() << "\n";

    std::cout << "[UB] " << ub_edge(10) << "\n";

    std::cout << "=== END ===\n";
}
