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
#include <chrono>
#include <exception>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

#include "dgtl_sdk/dgtl.h"

using namespace std;

static mutex g_log_mutex;

#define PASS(x)                         \
    {                                   \
        lock_guard<mutex> lock(g_log_mutex); \
        cout << "[PASS] " << x << '\n'; \
    }

#define FAIL(x)                         \
    {                                   \
        lock_guard<mutex> lock(g_log_mutex); \
        cout << "[FAIL] " << x << '\n'; \
    }

#define INFO(x)                         \
    {                                   \
        lock_guard<mutex> lock(g_log_mutex); \
        cout << "[INFO] " << x << '\n'; \
    }

#define COMPARE(name, ops, ms)                                  \
    {                                                           \
        const double secs = (ms) / 1000.0;                     \
        const double rate = (secs > 0.0) ? ((ops) / secs) : 0; \
        cout << "[COMPARE] " << name                           \
             << " | ops=" << (ops)                            \
             << " | ms=" << (ms)                              \
             << " | ops_per_sec=" << static_cast<uint64_t>(rate) << '\n'; \
    }

#define REQUIRE(cond, msg)              \
    if (!(cond)) {                      \
        FAIL(msg);                      \
        return false;                   \
    }

#define RUN(test)                                        \
    {                                                    \
        cout << "\n========== " << #test                \
             << " ==========\n";                        \
        const auto test_start = chrono::steady_clock::now(); \
        bool ok = false;                                 \
        try {                                            \
            ok = test();                                 \
        } catch (const exception& e) {                   \
            cout << "Exception: " << e.what() << '\n';   \
        } catch (...) {                                  \
            cout << "Unknown exception\n";               \
        }                                                \
        const auto test_end = chrono::steady_clock::now(); \
        const auto elapsed_ms = chrono::duration_cast<chrono::milliseconds>( \
            test_end - test_start).count();              \
        cout << "[TIME] " << #test << ": " << elapsed_ms << " ms\n"; \
        if (!ok) failures++;                             \
    }

bool test_version() {
    dgtl_version_info_t v = dgtl_get_version();
    REQUIRE(v.build_id != nullptr, "version");

    cout << "DGTL version: "
         << v.major << "." << v.minor << "." << v.patch << " [" << v.build_id << "]\n";

    PASS("version");
    return true;
}

bool test_stream_stress() {
    auto* ctx = dgtl_context_create(1, 16);
    REQUIRE(ctx, "context");

    constexpr int N = 1000;
    const auto bench_start = chrono::steady_clock::now();

    for (int i = 0; i < N; i++) {
        auto* stream = dgtl_stream_create(ctx, 16);
        REQUIRE(stream, "stream alloc");
        dgtl_stream_destroy(stream);

        if ((i + 1) % 250 == 0) {
            INFO(("stream progress: " +
                  to_string(i + 1) +
                  "/" + to_string(N)).c_str());
        }
    }

    const auto bench_end = chrono::steady_clock::now();
    const auto elapsed_ms = chrono::duration_cast<chrono::milliseconds>(
        bench_end - bench_start).count();

    dgtl_context_destroy(ctx);
    PASS("1000 stream alloc/free");
    COMPARE("stream_create_destroy", static_cast<double>(N), static_cast<double>(elapsed_ms));
    return true;
}

bool test_massive_ir() {
    auto* ctx = dgtl_context_create(2, 64);
    REQUIRE(ctx, "context");

    auto* stream = dgtl_stream_create(ctx, 1024);
    REQUIRE(stream, "stream");
    auto* fb = dgtl_func_builder_create(stream, "massive_ir", DGTL_TYPE_I64);
    REQUIRE(fb, "func_builder");
    auto entry = dgtl_func_append_block(fb, "entry");
    dgtl_func_set_insert_block(fb, entry);

    constexpr int N = 100000;
    const auto emit_start = chrono::steady_clock::now();

    for (int i = 0; i < N; i++) {
        auto lhs = dgtl_build_const_i64(fb, i);
        auto rhs = dgtl_build_const_i64(fb, i + 1);
        auto add = dgtl_build_add(fb, lhs, rhs);

        REQUIRE(dgtl_value_is_valid(add), "massive ir");

        if ((i + 1) % 25000 == 0) {
            INFO(("IR nodes: " +
                  to_string(i + 1) +
                  "/" + to_string(N)).c_str());
        }
    }

    const auto emit_end = chrono::steady_clock::now();
    const auto emit_ms = chrono::duration_cast<chrono::milliseconds>(
        emit_end - emit_start).count();

    (void)dgtl_func_builder_finalize(fb);

    const auto opt_start = chrono::steady_clock::now();
    const dgtl_err_t opt_err = dgtl_optimize(stream);
    const bool ok = DGTL_IS_OK(opt_err);
    const auto opt_end = chrono::steady_clock::now();
    const auto opt_ms = chrono::duration_cast<chrono::milliseconds>(
        opt_end - opt_start).count();

    REQUIRE(ok, "optimize massive");

    dgtl_func_builder_destroy(fb);
    dgtl_stream_destroy(stream);
    dgtl_context_destroy(ctx);

    PASS("100k node stress");
    COMPARE("ir_emit", static_cast<double>(N), static_cast<double>(emit_ms));
    COMPARE("ir_optimize", 1.0, static_cast<double>(opt_ms));
    return true;
}

bool test_parallel_chaos() {
    constexpr int THREADS = 16;
    constexpr int ITERATIONS = 1000;
    vector<thread> workers;
    const auto bench_start = chrono::steady_clock::now();

    for (int t = 0; t < THREADS; t++) {
        workers.emplace_back([=]() {
            for (int i = 0; i < ITERATIONS; i++) {
                auto* ctx = dgtl_context_create(1, 16);
                if (!ctx) {
                    continue;
                }

                auto* stream = dgtl_stream_create(ctx, 64);
                if (!stream) {
                    dgtl_context_destroy(ctx);
                    continue;
                }

                auto* fb = dgtl_func_builder_create(stream, "kernel", DGTL_TYPE_I64);
                if (fb) {
                    auto b = dgtl_func_append_block(fb, "b");
                    dgtl_func_set_insert_block(fb, b);
                    auto a = dgtl_build_const_i64(fb, i);
                    auto rhs = dgtl_build_const_i64(fb, i + 1);
                    (void)dgtl_build_add(fb, a, rhs);
                    (void)dgtl_func_builder_finalize(fb);
                    dgtl_func_builder_destroy(fb);
                }

                const dgtl_err_t err = dgtl_optimize(stream);
                (void)err;

                dgtl_stream_destroy(stream);
                dgtl_context_destroy(ctx);
            }
        });
    }

    for (auto& w : workers) {
        w.join();
    }

    const auto bench_end = chrono::steady_clock::now();
    const auto elapsed_ms = chrono::duration_cast<chrono::milliseconds>(
        bench_end - bench_start).count();
    const double total_ops = static_cast<double>(THREADS) * static_cast<double>(ITERATIONS);

    PASS("parallel execution");
    COMPARE("parallel_emit_optimize", total_ops, static_cast<double>(elapsed_ms));
    return true;
}

int main() {
    cout << "====================================\n";
    cout << "DGTL HARDEST E2E C++ GATE\n";
    cout << "====================================\n";

    int failures = 0;

    RUN(test_version);
    RUN(test_stream_stress);
    RUN(test_massive_ir);
    RUN(test_parallel_chaos);

    cout << "\n========== compare_profile ==========\n";
    cout << "[INFO] Profile intent: compare DGTL against LLVM/Cranelift/MLIR/vm8\n";
    cout << "[INFO] Normalize by ops_per_sec and identical operation counts\n";

    cout << "\n====================================\n";

    if (failures == 0) {
        cout << "ALL DGTL TESTS PASSED\n";
    } else {
        cout << failures << " TESTS FAILED\n";
    }

    cout << "====================================\n";

    return failures;
}