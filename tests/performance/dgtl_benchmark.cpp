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
#include <numeric>
#include <cmath>
#include <type_traits>

using namespace std;
using namespace std::chrono;

static const int N = 5'000'000;
volatile double g_result_sink = 0.0;

template<typename T>
inline void do_not_optimize(const T& value) {
#if defined(__GNUC__) || defined(__clang__)
    asm volatile("" : : "g"(value) : "memory");
#else
    (void)value;
#endif
}

inline void clobber_memory() {
#if defined(__GNUC__) || defined(__clang__)
    asm volatile("" : : : "memory");
#endif
}

template<typename T>
inline void consume_result(const T& result) {
    if constexpr (std::is_arithmetic_v<T>) {
        g_result_sink = g_result_sink + static_cast<double>(result);
    }
    do_not_optimize(result);
    clobber_memory();
}

// -------------------------------
// TEST 1: Dependency Chain
// -------------------------------
float dependency_chain() {
    float x = 1.0f;
    for (int i = 0; i < N; i++) {
        x = x * 1.000001f + 0.000001f;
    }
    return x;
}

// -------------------------------
// TEST 2: ECS-like Update (SoA style simulation)
// -------------------------------
struct Entity {
    float x, y, z;
};

float ecs_update(vector<Entity>& e) {
    float sum = 0.0f;
    for (auto &v : e) {
        v.x += 0.01f;
        v.y *= 1.0001f;
        v.z = v.x + v.y;
        sum += v.z;
    }
    return sum;
}

// -------------------------------
// TEST 3: SIMD-like Kernel (manual vector pattern)
// -------------------------------
float simd_kernel(const vector<float>& a,
                  const vector<float>& b,
                  vector<float>& out) {
    float sum = 0.0f;
    for (size_t i = 0; i < a.size(); i++) {
        out[i] = a[i] * b[i] + 1.0f;
        sum += out[i];
    }
    return sum;
}

// -------------------------------
// TEST 4: HFT-style Branching
// -------------------------------
int hft_branching(const vector<int>& data) {
    int acc = 0;
    for (auto v : data) {
        if (v % 2 == 0)
            acc += v;
        else
            acc -= v;
    }
    return acc;
}

// -------------------------------
// MAIN BENCH RUNNER
// -------------------------------
template<typename F>
double bench(const string& name, F fn) {
    auto start = high_resolution_clock::now();
    auto result = fn();
    consume_result(result);
    auto end = high_resolution_clock::now();

    double t = duration<double>(end - start).count();
    cout << "[" << name << "] " << t << " sec" << endl;

    return t;
}

int main() {
    cout << "==============================\n";
    cout << " DGTL BENCHMARK SUITE (FAIR)\n";
    cout << "==============================\n\n";

    vector<Entity> ecs(N);
    vector<float> a(N, 1.01f), b(N, 2.02f), out(N);
    vector<int> data(N);

    std::mt19937 rng(42);
    std::uniform_int_distribution<int> dist(1, 100);

    for (int i = 0; i < N; i++)
        data[i] = dist(rng);

    bench("Dependency Chain", [] {
        return dependency_chain();
    });

    bench("ECS Update", [&] {
        return ecs_update(ecs);
    });

    bench("SIMD Kernel", [&] {
        return simd_kernel(a, b, out);
    });

    bench("HFT Branching", [&] {
        return hft_branching(data);
    });

    cout << "\nDONE.\n";
}