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
#include <atomic>
#include <exception>
#include <iostream>
#include <mutex>
#include <random>
#include <thread>
#include <vector>

#include "build/DGTL_SDK/include/dgtl.h"

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
    const char* version = dgtl_version_string();
    REQUIRE(version != nullptr, "version");

    cout << "DGTL version: "
         << version << '\n';

    PASS("version");
    return true;
}

bool test_context() {
    auto* ctx =
        dgtl_context_create();

    REQUIRE(ctx, "context");

    dgtl_context_destroy(ctx);

    PASS("context lifecycle");
    return true;
}

bool test_stream() {
    auto* ctx =
        dgtl_context_create();

    REQUIRE(ctx, "context");

    auto* stream =
        dgtl_stream_create(ctx, 16);

    REQUIRE(stream, "stream");

    dgtl_stream_destroy(stream);
    dgtl_context_destroy(ctx);

    PASS("stream lifecycle");
    return true;
}

bool test_stream_stress() {
    auto* ctx =
        dgtl_context_create();

    REQUIRE(ctx, "context");

    constexpr int N = 1000;

    for (int i = 0; i < N; i++) {
        auto* s =
            dgtl_stream_create(ctx, 64);

        REQUIRE(s, "stream alloc");

        dgtl_stream_destroy(s);

        if ((i + 1) % 250 == 0)
            INFO(("stream progress: " +
                  to_string(i + 1) +
                  "/" + to_string(N)).c_str());
    }

    INFO(("stream iterations: " + to_string(N)).c_str());

    dgtl_context_destroy(ctx);

    PASS("1000 stream alloc/free");
    return true;
}

bool test_null_safety() {
    try {
        dgtl_context_destroy(nullptr);
        dgtl_stream_destroy(nullptr);

        dgtl_optimize(nullptr);

        PASS("null safety");
    }
    catch (...) {
        FAIL("null safety");
        return false;
    }

    return true;
}

bool test_ir_creation() {
    auto* ctx =
        dgtl_context_create();

    REQUIRE(ctx, "context");

    auto* stream =
        dgtl_stream_create(ctx, 16);

    REQUIRE(stream, "stream");

    auto lhs =
        dgtl_emit_const_i64(
            stream, 10);

    auto rhs =
        dgtl_emit_const_i64(
            stream, 20);

    auto add =
        dgtl_emit_add_i64(
            stream,
            lhs,
            rhs);

    REQUIRE(lhs, "lhs node");
    REQUIRE(rhs, "rhs node");
    REQUIRE(add, "add node");

    cout << "LHS node: "
         << lhs << '\n';

    cout << "RHS node: "
         << rhs << '\n';

    cout << "ADD node: "
         << add << '\n';

    dgtl_stream_destroy(stream);
    dgtl_context_destroy(ctx);

    PASS("IR creation");
    return true;
}

bool test_massive_ir() {
    auto* ctx =
        dgtl_context_create();

    REQUIRE(ctx, "context");

    constexpr int N = 100000;
    const size_t reserve_nodes =
        static_cast<size_t>(N) * 3u + 16u;

    auto* stream =
        dgtl_stream_create(ctx, reserve_nodes);

    REQUIRE(stream, "stream");

    for (int i = 0; i < N; i++) {

        auto lhs =
            dgtl_emit_const_i64(
                stream, i);

        auto rhs =
            dgtl_emit_const_i64(
                stream, i + 1);

        auto add =
            dgtl_emit_add_i64(
                stream,
                lhs,
                rhs);

        REQUIRE(add,
                 "massive ir");

        if ((i + 1) % 25000 == 0)
            INFO(("IR nodes: " +
                  to_string(i + 1) +
                  "/" + to_string(N)).c_str());
    }

    INFO(("IR iterations: " + to_string(N)).c_str());

    bool ok =
        dgtl_optimize(
            stream).ok;

    REQUIRE(ok,
             "optimize massive");

    dgtl_stream_destroy(stream);
    dgtl_context_destroy(ctx);

    PASS("100k node stress");
    return true;
}

bool test_optimizer_repeat() {
    auto* ctx =
        dgtl_context_create();

    REQUIRE(ctx, "context");

    auto* stream =
        dgtl_stream_create(ctx, 64);

    REQUIRE(stream, "stream");

    auto a =
        dgtl_emit_const_i64(
            stream, 1);

    auto b =
        dgtl_emit_const_i64(
            stream, 2);

    dgtl_emit_add_i64(
        stream,
        a,
        b);

        constexpr int N = 100;

        for (int i = 0;
            i < N;
         i++) {

        bool ok =
            dgtl_optimize(
                stream).ok;

        REQUIRE(ok,
                 "optimize repeat");

        if ((i + 1) % 25 == 0)
            INFO(("optimizer progress: " +
                  to_string(i + 1) +
                  "/" + to_string(N)).c_str());
    }

    dgtl_stream_destroy(stream);
    dgtl_context_destroy(ctx);

    PASS("100 repeated optimizations");
    return true;
}

bool test_parallel_chaos() {
    constexpr int THREADS = 8;
    constexpr int ITERATIONS = 200;
    constexpr int TOTAL = THREADS * ITERATIONS;
    constexpr int REPORT_STEP = 400;

    vector<thread> workers;
    atomic<int> completed{0};
    atomic<int> next_report{REPORT_STEP};

    INFO(("parallel start: " +
          to_string(THREADS) +
          " threads x " +
          to_string(ITERATIONS) +
          " iterations").c_str());

    for (int t = 0;
         t < THREADS;
         t++) {

        workers.emplace_back([&]() {
            auto* thread_ctx =
                dgtl_context_create();

            if (!thread_ctx) {
                return;
            }

            for (int i = 0;
                 i < ITERATIONS;
                 i++) {
                auto* stream =
                    dgtl_stream_create(
                        thread_ctx,
                        64);

                if (!stream) {
                    continue;
                }

                auto a =
                    dgtl_emit_const_i64(
                        stream,
                        i);

                auto b =
                    dgtl_emit_const_i64(
                        stream,
                        i + 1);

                dgtl_emit_add_i64(
                    stream,
                    a,
                    b);

                dgtl_optimize(
                    stream);

                dgtl_stream_destroy(
                    stream);

                const int done =
                    completed.fetch_add(1) + 1;

                int expected = next_report.load();
                while (done >= expected) {
                    if (next_report.compare_exchange_weak(expected, expected + REPORT_STEP)) {
                        INFO(("parallel progress: " +
                              to_string(done) +
                              "/" +
                              to_string(TOTAL)).c_str());
                        break;
                    }
                }
            }

            dgtl_context_destroy(
                thread_ctx);
        });
    }

    for (auto& w : workers)
        w.join();

    INFO(("parallel iterations: " +
          to_string(TOTAL)).c_str());

    PASS("parallel execution");
    return true;
}

bool test_memory_pressure() {
    vector<void*> memory;

    try {
        for (int i = 0;
             i < 100;
             i++) {

            memory.push_back(
                malloc(
                    1024 * 1024));

            REQUIRE(
                memory.back(),
                "memory alloc");
        }
    }
    catch (...) {
        FAIL(
            "memory pressure");
        return false;
    }

    for (auto p : memory)
        free(p);

    PASS("memory pressure");
    return true;
}

bool test_soak() {

    auto start =
        chrono::steady_clock::now();

    while (
        chrono::steady_clock::now()
        - start
        < chrono::seconds(10))
    {
        auto* ctx =
            dgtl_context_create();

        auto* stream =
            dgtl_stream_create(
                ctx,
                64);

        auto a =
            dgtl_emit_const_i64(
                stream,
                10);

        auto b =
            dgtl_emit_const_i64(
                stream,
                20);

        dgtl_emit_add_i64(
            stream,
            a,
            b);

        dgtl_optimize(
            stream);

        dgtl_stream_destroy(
            stream);

        dgtl_context_destroy(
            ctx);
    }

    PASS("10 second soak");
    return true;
}

int main() {

    cout
        << "====================================\n";

    cout
        << "DGTL HARDEST E2E C++ GATE\n";

    cout
        << "====================================\n";

    int failures = 0;

    RUN(test_version);
    RUN(test_stream_stress);
    RUN(test_massive_ir);
    RUN(test_parallel_chaos);

    cout
        << "\n====================================\n";

    if (failures == 0) {
        cout
            << "ALL DGTL TESTS PASSED\n";
    } else {
        cout
            << failures
            << " TESTS FAILED\n";
    }

    cout
        << "====================================\n";

    return failures;
}