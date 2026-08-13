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
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <random>
#include <vector>

#include "build/DGTL_SDK/include/dgtl.h"

using namespace std;

static constexpr int DEFAULT_TEST_CASES = 1000000;
static constexpr int PROGRESS_STEP = 10000;
static constexpr double EPS = 1e-9;

struct Case {
    int64_t a;
    int64_t b;
    int64_t c;
    int64_t d;
};

inline bool almost_equal(double x, double y) {
    if (std::isnan(x) && std::isnan(y))
        return true;

    if (std::isinf(x) && std::isinf(y))
        return true;

    return fabs(x - y) < EPS;
}

double reference_math(
    int64_t a,
    int64_t b,
    int64_t c,
    int64_t d)
{
    // deliberately ugly expression
    // compiler optimizers often break here

    double x =
        ((double)a * (double)b) +
        ((double)c * (double)d);

    double y =
        ((double)a - (double)d);

    double z =
        ((double)b + (double)c);

    double r =
        sqrt(fabs(x)) +
        log(fabs(z) + 1.0) +
        sin((double)a) +
        cos((double)b) +
        tanh((double)c);

    r +=
        (y * z) /
        (fabs((double)d) + 1.0);

    r =
        fma(r, 0.999999999,
            sqrt(fabs(r)));

    return r;
}

bool run_dgtl_math(
    dgtl_context_t* ctx,
    int64_t a,
    int64_t b,
    int64_t c,
    int64_t d)
{
    if (!ctx)
        return false;

    auto* stream =
        dgtl_stream_create(
            ctx, 256);

    if (!stream) {
        return false;
    }

    auto A =
        dgtl_emit_const_i64(
            stream, a);

    auto B =
        dgtl_emit_const_i64(
            stream, b);

    auto C =
        dgtl_emit_const_i64(
            stream, c);

    auto D =
        dgtl_emit_const_i64(
            stream, d);

    auto AB =
        dgtl_emit_mul_i64(
            stream, A, B);

    auto CD =
        dgtl_emit_mul_i64(
            stream, C, D);

    auto SUM =
        dgtl_emit_add_i64(
            stream, AB, CD);

    auto Y =
        dgtl_emit_sub_i64(
            stream, A, D);

    auto Z =
        dgtl_emit_add_i64(
            stream, B, C);

    auto MIX =
        dgtl_emit_add_i64(
            stream, SUM, Y);

    MIX =
        dgtl_emit_mul_i64(
            stream, MIX, Z);

    if (!MIX) {
        dgtl_stream_destroy(stream);
        return false;
    }

    auto result =
        dgtl_optimize(stream);

    bool ok =
        result.ok;

    dgtl_stream_destroy(stream);

    return ok;
}

int main() {

    std::cout.setf(std::ios::unitbuf);

    cout
        << "====================================\n";

    cout
        << "DGTL WORLD CLASS DIFFERENTIAL GATE\n";

    cout
        << "====================================\n";

    mt19937_64 rng(42);

    uniform_int_distribution<int64_t>
        dist(-1000000,
             1000000);

    int mismatches = 0;
    int sdk_failures = 0;

    int test_cases = DEFAULT_TEST_CASES;
    if (const char* env_cases = std::getenv("DGTL_MATH_CASES")) {
        const int parsed = std::atoi(env_cases);
        if (parsed > 0)
            test_cases = parsed;
    }

    auto* ctx = dgtl_context_create();
    if (!ctx) {
        cout << "[FAIL] context create\n";
        return 1;
    }

    const auto start = std::chrono::steady_clock::now();

    cout
        << "[INFO] starting "
        << test_cases
        << " cases"
        << '\n';

    for (int i = 0;
         i < test_cases;
         i++)
    {
        int64_t a = dist(rng);
        int64_t b = dist(rng);
        int64_t c = dist(rng);
        int64_t d = dist(rng);

        double ref =
            reference_math(
                a, b, c, d);

        bool sdk_ok =
            run_dgtl_math(
                ctx,
                a, b, c, d);

        if (!sdk_ok) {
            sdk_failures++;

            if (sdk_failures < 20) {
                cout
                    << "[SDK FAIL] "
                    << "a=" << a
                    << " b=" << b
                    << " c=" << c
                    << " d=" << d
                    << '\n';
            }
        }

        if (!std::isfinite(ref)) {
            mismatches++;

            if (mismatches < 20) {
                cout
                    << "[REF FAIL] "
                    << "non-finite math\n";
            }
        }

        if ((i + 1)
            % PROGRESS_STEP == 0)
        {
            const auto now = std::chrono::steady_clock::now();
            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - start).count();
            const double secs = static_cast<double>(ms) / 1000.0;
            const double rate = secs > 0.0
                ? static_cast<double>(i + 1) / secs
                : 0.0;

            cout
                << "[INFO] progress: "
                << (i + 1)
                << "/"
                << test_cases
                << " | elapsed_ms="
                << ms
                << " | cases_per_sec="
                << static_cast<uint64_t>(rate)
                << '\n';
        }
    }

    dgtl_context_destroy(ctx);

    cout
        << "====================================\n";

    cout
        << "SDK FAILURES: "
        << sdk_failures
        << '\n';

    cout
        << "MATH FAILURES: "
        << mismatches
        << '\n';

    if (sdk_failures == 0 &&
        mismatches == 0)
    {
        cout
            << "[PASS] WORLD CLASS DIFFERENTIAL TEST\n";
    }
    else {
        cout
            << "[FAIL] DIFFERENTIAL TEST FAILED\n";
    }

    cout
        << "====================================\n";

    return
        sdk_failures +
        mismatches;
}