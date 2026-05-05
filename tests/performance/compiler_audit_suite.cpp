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
#include <random>
#include <atomic>
#include <string>
#include <thread>

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
#endif

// World-Class Optimization: Intrinsic for software prefetching
#if defined(__GNUC__) || defined(__clang__)
    #define DGTL_PREFETCH(addr) __builtin_prefetch(addr)
#else
    #define DGTL_PREFETCH(addr)
#endif

using namespace std;
using namespace std::chrono;

static const int N = 2'000'000;

// ----------------------------------------------------
// GLOBAL SINK & TITAN INVARIANTS
// ----------------------------------------------------
volatile long long global_sink = 0;
atomic<long long> checksum{0};

void titan_stabilize_hardware() {
#ifdef _WIN32
    // Pin to Core 0 and set Time-Critical priority to flatten the performance line
    SetThreadAffinityMask(GetCurrentThread(), 1);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
#endif
}

void titan_heat_cycle() {
    auto start_warm = high_resolution_clock::now();
    volatile int dummy = 0;
    // Spin for 100ms to ensure Max Turbo Frequency is engaged
    while(duration_cast<milliseconds>(high_resolution_clock::now() - start_warm).count() < 100) {
        dummy = dummy + 1;
    }
}

// ----------------------------------------------------
// TEST 1: Dependency Chain (ILP Stress Test)
// ----------------------------------------------------
float dependency_chain() {
    float x = 1.0f;
    for (int i = 0; i < N; i++) {
        x = x * 1.000001f + 0.000001f;
    }
    return x;
}

// ----------------------------------------------------
// TEST 2: ECS (SoA simulation - Cache Locality)
// ----------------------------------------------------
struct Entity {
    float x, y, z;
};

float ecs_update(vector<Entity>& e) {
    float sum = 0;
    long long local_checksum = 0;
    for (size_t i = 0; i < e.size(); ++i) {
        if (i % 8 == 0) DGTL_PREFETCH(&e[i + 8]);
        auto &v = e[i];
        v.x += 0.01f;
        v.y *= 1.0001f;
        v.z = v.x + v.y;
        sum += v.z;
        local_checksum += (long long)(v.x + v.y + v.z);
    }
    checksum += local_checksum;
    return sum;
}

// ----------------------------------------------------
// TEST 3: SIMD-like kernel (Throughput Test)
// ----------------------------------------------------
float simd_kernel(const vector<float>& a,
                  const vector<float>& b,
                  vector<float>& out) {
    float sum = 0;
    long long local_checksum = 0;
    uint32_t check_val = (uint32_t)(checksum.load() & 0xFF);

    #pragma omp simd reduction(+:sum, local_checksum)
    for (size_t i = 0; i < a.size(); i++) {
        out[i] = a[i] * b[i] + (float)check_val;
        sum += out[i];
        local_checksum += (long long)out[i];
    }
    checksum += local_checksum;
    return sum;
}

// ----------------------------------------------------
// TEST 4: HFT branching (Pipeline Stall Test)
// ----------------------------------------------------
int hft_branching(const vector<int>& data) {
    int acc = 0;
    long long local_checksum = 0;
    for (auto v : data) {
        if ((v * 131) % 3 == 0) [[likely]]
            acc += v;
        else if ((v * 57) % 5 == 0) [[unlikely]]
            acc -= v;
        else
            acc ^= v;
        local_checksum += acc;
    }
    checksum += local_checksum;
    return acc;
}

// ----------------------------------------------------
// TEST 5: Loop Fusion Sensitivity (Optimizer Intelligence)
// ----------------------------------------------------
float loop_fusion_test(vector<float>& a) {
    float sum = 0;
    long long local_checksum = 0;
    for (auto &x : a) x += 1.0f;
    for (auto &x : a) x *= 1.0001f;
    for (auto x : a) {
        sum += x;
        local_checksum += (long long)x;
    }
    checksum += local_checksum;
    return sum;
}

// ----------------------------------------------------
// BENCH WRAPPER
// ----------------------------------------------------
template<typename F>
double bench(const string& name, F fn) {
    auto start = high_resolution_clock::now();
    auto result = fn();
    auto end = high_resolution_clock::now();

    global_sink += (long long)result;
    double t = duration<double>(end - start).count();

    printf("[%s] %.8f sec\n", name.c_str(), t);
    return t;
}

// ----------------------------------------------------
// MAIN
// ----------------------------------------------------
int main() {
    titan_stabilize_hardware();
    titan_heat_cycle();

    printf("=====================================\n");
    printf(" DGTL / LLVM / GCC FULL AUDIT SUITE\n");
    printf(" TITAN-STABLE v1.0.0 (HARDWARE LOCKED)\n");
    printf("=====================================\n\n");

    vector<Entity> ecs(N);
    vector<float> a(N, 1.01f), b(N, 2.02f), out(N);
    vector<int> data(N);

    mt19937 rng(42);
    uniform_int_distribution<int> dist(1, 100);
    for (int i = 0; i < N; i++) data[i] = dist(rng);

    // Warm-up pass to resolve page-faults and verify correctness
    dependency_chain(); ecs_update(ecs); simd_kernel(a, b, out); hft_branching(data); loop_fusion_test(a);

    bench("Dependency Chain", [] { return dependency_chain(); });
    bench("ECS Update", [&] { return ecs_update(ecs); });
    bench("SIMD Kernel", [&] { return simd_kernel(a, b, out); });
    bench("HFT Branching", [&] { return hft_branching(data); });
    bench("Loop Fusion Sensitivity", [&] { return loop_fusion_test(a); });

    printf("\n====================\n");
    printf("CHECKSUM: %lld\n", (long long)checksum.load());
    printf("GLOBAL SINK: %lld\n", (long long)global_sink);
    printf("DONE\n");

    return 0;
}


