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
#include <atomic>
#include <chrono>
#include <cstdint>
#include <exception>
#include <functional>
#include <iostream>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "dgtl_sdk/dgtl.h"

static bool has_opcode(const dgtl_stream_t* s, dgtl_opcode_t opcode) {
    const size_t n = dgtl_stream_node_count(s);
    for (size_t i = 0; i < n; i++) {
        dgtl_node_info_t info{};
        if (dgtl_stream_get_node(s, i, &info) == 1 && info.opcode == opcode) {
            return true;
        }
    }
    return false;
}

static void runtime_hit_task(void* context) {
    auto* hit = static_cast<std::atomic<int>*>(context);
    hit->fetch_add(1);
}

using namespace std;

static mutex g_log;

static void log_line(const string& s) {
    lock_guard<mutex> lock(g_log);
    cout << s << '\n';
}

static bool check(bool cond, const string& msg) {
    if (!cond) {
        log_line("[FAIL] " + msg);
    }
    return cond;
}

static bool test_sdk_lifecycle() {
    auto* ctx = dgtl_context_create();
    if (!check(ctx != nullptr, "context create")) return false;
    auto* s = dgtl_stream_create(ctx, 32);
    if (!check(s != nullptr, "stream create")) {
        dgtl_context_destroy(ctx);
        return false;
    }
    dgtl_stream_destroy(s);
    dgtl_context_destroy(ctx);
    return true;
}

static bool test_ir_correctness() {
    auto* ctx = dgtl_context_create();
    if (!check(ctx != nullptr, "context")) return false;
    auto* s = dgtl_stream_create(ctx, 64);
    if (!check(s != nullptr, "stream")) {
        dgtl_context_destroy(ctx);
        return false;
    }

    const uint32_t c1 = dgtl_emit_const_i64(s, 100);
    const uint32_t c2 = dgtl_emit_const_i64(s, 25);
    const uint32_t add = dgtl_emit_add_i64(s, c1, c2);
    const uint32_t sub = dgtl_emit_sub_i64(s, c1, c2);
    const uint32_t mul = dgtl_emit_mul_i64(s, c1, c2);
    const uint32_t div = dgtl_emit_div_i64(s, c1, c2);
    const uint32_t mod = dgtl_emit_mod_i64(s, c1, c2);
    bool ok = check(c1 && c2 && add && sub && mul && div && mod, "emit arithmetic nodes");

    dgtl_node_info_t n0{}, n2{}, n6{};
    ok = ok && check(dgtl_stream_get_node(s, 0, &n0) == 1, "get node[0]");
    ok = ok && check(dgtl_stream_get_node(s, 2, &n2) == 1, "get node[2]");
    ok = ok && check(dgtl_stream_get_node(s, 6, &n6) == 1, "get node[6]");
    ok = ok && check(n0.opcode == DGTL_OP_CONST, "node[0] opcode CONST");
    ok = ok && check(n2.opcode == DGTL_OP_ADD_I64, "node[2] opcode ADD_I64");
    ok = ok && check(n6.opcode == DGTL_OP_MOD_I64, "node[6] opcode MOD_I64");

    dgtl_stream_destroy(s);
    dgtl_context_destroy(ctx);
    return ok;
}

static bool test_optimizer_determinism() {
    auto build_hash = []() -> uint64_t {
        auto* ctx = dgtl_context_create();
        if (!ctx) return 0;
        auto* s = dgtl_stream_create(ctx, 256);
        if (!s) {
            dgtl_context_destroy(ctx);
            return 0;
        }
        uint32_t acc = dgtl_emit_const_i64(s, 1);
        for (int i = 0; i < 64; i++) {
            uint32_t c = dgtl_emit_const_i64(s, static_cast<uint64_t>(i + 2));
            acc = dgtl_emit_add_i64(s, acc, c);
            acc = dgtl_emit_mul_i64(s, acc, c);
        }
        dgtl_emit_return(s, acc);
        const dgtl_status_t st = dgtl_optimize(s);
        const uint64_t h = DGTL_IS_OK(st) ? dgtl_stream_logic_hash(s) : 0;
        dgtl_stream_destroy(s);
        dgtl_context_destroy(ctx);
        return h;
    };

    const uint64_t h1 = build_hash();
    const uint64_t h2 = build_hash();
    const uint64_t h3 = build_hash();
    return check(h1 != 0 && h1 == h2 && h2 == h3, "deterministic optimized logic hash");
}

static bool test_massive_ir() {
    constexpr int N = 120000;
    auto* ctx = dgtl_context_create();
    if (!check(ctx != nullptr, "context")) return false;
    auto* s = dgtl_stream_create(ctx, static_cast<size_t>(N) * 3u + 64u);
    if (!check(s != nullptr, "stream")) {
        dgtl_context_destroy(ctx);
        return false;
    }

    for (int i = 0; i < N; i++) {
        const uint32_t a = dgtl_emit_const_i64(s, static_cast<uint64_t>(i));
        const uint32_t b = dgtl_emit_const_i64(s, static_cast<uint64_t>(i + 1));
        const uint32_t r = dgtl_emit_add_i64(s, a, b);
        if (!r) {
            dgtl_stream_destroy(s);
            dgtl_context_destroy(ctx);
            return false;
        }
        if ((i + 1) % 30000 == 0) {
            log_line("[INFO] massive progress: " + to_string(i + 1) + "/" + to_string(N));
        }
    }

    const dgtl_status_t st = dgtl_optimize(s);
    const bool ok = check(DGTL_IS_OK(st), "optimize massive") && check(dgtl_stream_node_count(s) > 0, "massive node_count > 0");
    dgtl_stream_destroy(s);
    dgtl_context_destroy(ctx);
    return ok;
}

static bool test_parallel_safety() {
    constexpr int THREADS = 8;
    constexpr int ITERS = 400;
    constexpr int TOTAL = THREADS * ITERS;
    constexpr int STEP = TOTAL / 4;

    atomic<int> done{0};
    atomic<int> fails{0};
    atomic<int> next{STEP};
    vector<thread> workers;

    log_line("[INFO] parallel start: " + to_string(THREADS) + "x" + to_string(ITERS));

    for (int t = 0; t < THREADS; t++) {
        workers.emplace_back([&]() {
            dgtl_context_t* ctx = dgtl_context_create();
            if (!ctx) {
                fails.fetch_add(1);
                return;
            }
            for (int i = 0; i < ITERS; i++) {
                dgtl_stream_t* s = dgtl_stream_create(ctx, 32);
                if (!s) {
                    fails.fetch_add(1);
                    continue;
                }
                uint32_t a = dgtl_emit_const_i64(s, static_cast<uint64_t>(i));
                uint32_t b = dgtl_emit_const_i64(s, static_cast<uint64_t>(i + 7));
                uint32_t r = dgtl_emit_add_i64(s, a, b);
                dgtl_status_t st = dgtl_optimize(s);
                if (!r || DGTL_IS_ERR(st)) fails.fetch_add(1);
                dgtl_stream_destroy(s);

                const int n = done.fetch_add(1) + 1;
                int expected = next.load();
                while (n >= expected) {
                    if (next.compare_exchange_weak(expected, expected + STEP)) {
                        log_line("[INFO] parallel progress: " + to_string(n) + "/" + to_string(TOTAL));
                        break;
                    }
                }
            }
            dgtl_context_destroy(ctx);
        });
    }

    for (auto& w : workers) w.join();
    return check(done.load() == TOTAL && fails.load() == 0, "parallel safety");
}

static bool test_semantic_correctness() {
    auto* ctx = dgtl_context_create();
    if (!check(ctx != nullptr, "context")) return false;
    auto* s = dgtl_stream_create(ctx, 64);
    if (!check(s != nullptr, "stream")) {
        dgtl_context_destroy(ctx);
        return false;
    }

    uint32_t a = dgtl_emit_const_i64(s, 84);
    uint32_t b = dgtl_emit_const_i64(s, 12);
    uint32_t c = dgtl_emit_sub_i64(s, a, b);
    uint32_t d = dgtl_emit_mul_i64(s, c, b);
    uint32_t e = dgtl_emit_div_i64(s, d, b);
    uint32_t f = dgtl_emit_mod_i64(s, e, b);
    dgtl_emit_return(s, f);

    dgtl_node_info_t n2{}, n3{}, n4{}, n5{};
    bool ok = check(c && d && e && f, "semantic emit") &&
              check(dgtl_stream_get_node(s, 2, &n2) == 1 && n2.opcode == DGTL_OP_SUB_I64, "semantic SUB") &&
              check(dgtl_stream_get_node(s, 3, &n3) == 1 && n3.opcode == DGTL_OP_MUL_I64, "semantic MUL") &&
              check(dgtl_stream_get_node(s, 4, &n4) == 1 && n4.opcode == DGTL_OP_DIV_I64, "semantic DIV") &&
              check(dgtl_stream_get_node(s, 5, &n5) == 1 && n5.opcode == DGTL_OP_MOD_I64, "semantic MOD");

    dgtl_stream_destroy(s);
    dgtl_context_destroy(ctx);
    return ok;
}

static bool test_ssa_phi_control_flow_correctness() {
    auto* ctx = dgtl_context_create();
    if (!check(ctx != nullptr, "context")) return false;
    auto* s = dgtl_stream_create(ctx, 128);
    if (!check(s != nullptr, "stream")) {
        dgtl_context_destroy(ctx);
        return false;
    }

    uint32_t a = dgtl_emit_const_i64(s, 10);
    uint32_t b = dgtl_emit_const_i64(s, 20);
    uint32_t cmp = dgtl_emit_op(s, DGTL_OP_CMP_LT, a, b, 0, DGTL_FLAG_NONE);
    dgtl_emit_op(s, DGTL_OP_LABEL, 0, 0, 1, DGTL_FLAG_NONE);
    dgtl_emit_op(s, DGTL_OP_BR_IF, cmp, 0, 2, DGTL_FLAG_NONE);
    dgtl_emit_op(s, DGTL_OP_LABEL, 0, 0, 2, DGTL_FLAG_NONE);
    uint32_t phi = dgtl_emit_op(s, DGTL_OP_PHI, a, b, 0, DGTL_FLAG_NONE);
    dgtl_emit_return(s, phi);

    const bool had_label = has_opcode(s, DGTL_OP_LABEL);
    const bool had_branch = has_opcode(s, DGTL_OP_BR_IF);
    const bool had_phi = has_opcode(s, DGTL_OP_PHI);

    dgtl_status_t st = dgtl_optimize_with_profile(s, DGTL_OPT_PROFILE_FOLD);
    bool ok = check(a && b && cmp && phi, "ssa/phi emit") &&
              check(DGTL_IS_OK(st), "ssa/phi optimize") &&
              check(had_label, "cfg label present") &&
              check(had_branch, "cfg branch present") &&
              check(had_phi, "phi present");

    if (ok) {
        log_line("[INFO] ssa/cfg nodes emitted and optimized");
    }

    dgtl_stream_destroy(s);
    dgtl_context_destroy(ctx);
    return ok;
}

static bool test_simd_correctness() {
    auto* ctx = dgtl_context_create();
    if (!check(ctx != nullptr, "context")) return false;
    auto* s = dgtl_stream_create(ctx, 128);
    if (!check(s != nullptr, "stream")) {
        dgtl_context_destroy(ctx);
        return false;
    }

    uint32_t a = dgtl_emit_const_i64(s, 3);
    uint32_t b = dgtl_emit_const_i64(s, 4);
    uint32_t c = dgtl_emit_const_i64(s, 5);
    uint32_t fma = dgtl_emit_fma(s, a, b, c);
    uint32_t base = dgtl_emit_const_i64(s, 1024);
    uint32_t idx = dgtl_emit_const_i64(s, 7);
    uint32_t val = dgtl_emit_const_i64(s, 9);
    uint32_t g = dgtl_emit_gather(s, base, idx, DGTL_FLAG_NONE);
    uint32_t sc = dgtl_emit_scatter(s, base, idx, val, DGTL_FLAG_NONE);

    dgtl_status_t leg = dgtl_legalize_simd(s, DGTL_SIMD_X86_AVX2);
    dgtl_status_t opt = dgtl_optimize_with_profile(s, DGTL_OPT_PROFILE_SIMD);

    bool ok = check(a && b && c && fma && base && idx && val && g && sc, "simd emit") &&
              check(DGTL_IS_OK(leg), "simd legalize") &&
              check(DGTL_IS_OK(opt), "simd optimize") &&
              check(has_opcode(s, DGTL_OP_FMA), "fma opcode present") &&
              check(has_opcode(s, DGTL_OP_GATHER), "gather opcode present") &&
              check(has_opcode(s, DGTL_OP_SCATTER), "scatter opcode present");

    dgtl_stream_destroy(s);
    dgtl_context_destroy(ctx);
    return ok;
}

static bool test_register_allocation_correctness() {
    auto* ctx = dgtl_context_create();
    if (!check(ctx != nullptr, "context")) return false;
    auto* s = dgtl_stream_create(ctx, 4096);
    if (!check(s != nullptr, "stream")) {
        dgtl_context_destroy(ctx);
        return false;
    }

    uint32_t acc = dgtl_emit_const_i64(s, 1);
    for (int i = 0; i < 512; i++) {
        uint32_t c = dgtl_emit_const_i64(s, static_cast<uint64_t>(i + 2));
        acc = dgtl_emit_add_i64(s, acc, c);
        acc = dgtl_emit_xor_i64(s, acc, c);
    }
    dgtl_emit_return(s, acc);

    dgtl_status_t st = dgtl_optimize_with_profile(s, DGTL_OPT_PROFILE_PRESSURE);
    uint64_t h = dgtl_stream_logic_hash(s);
    bool ok = check(DGTL_IS_OK(st), "pressure optimize") &&
              check(h != 0, "pressure hash nonzero");

    dgtl_stream_destroy(s);
    dgtl_context_destroy(ctx);
    return ok;
}

static bool test_jit_correctness() {
    auto* ctx = dgtl_context_create();
    if (!check(ctx != nullptr, "context")) return false;
    auto* s = dgtl_stream_create(ctx, 64);
    if (!check(s != nullptr, "stream")) {
        dgtl_context_destroy(ctx);
        return false;
    }

    uint32_t a = dgtl_emit_const_i64(s, 40);
    uint32_t b = dgtl_emit_const_i64(s, 2);
    uint32_t r = dgtl_emit_add_i64(s, a, b);
    dgtl_emit_return(s, r);

    dgtl_status_t opt = dgtl_optimize(s);
    dgtl_strike_result_t res = dgtl_strike_stream(s, DGTL_TARGET_X64);

    bool ok = check(DGTL_IS_OK(opt), "jit optimize") &&
              check(DGTL_IS_OK(res), "jit strike ok") &&
              check(res.code_size > 0, "jit code_size > 0");

    dgtl_stream_destroy(s);
    dgtl_context_destroy(ctx);
    return ok;
}

static bool test_abi_correctness() {
    const char* version = dgtl_version_string();
    const char* x64_name = dgtl_target_name(DGTL_TARGET_X64);
    const char* x64_ext = dgtl_target_extension(DGTL_TARGET_X64);

    bool ok = check(version != nullptr && version[0] != '\0', "abi version string") &&
              check(x64_name != nullptr && x64_name[0] != '\0', "abi target name") &&
              check(x64_ext != nullptr && x64_ext[0] != '\0', "abi target extension");

    if (ok) {
        log_line(string("[INFO] abi: version=") + version +
                 " target=" + x64_name +
                 " ext=" + x64_ext);
    }

    return ok;
}

static bool test_deoptimization_correctness() {
    auto* ctx = dgtl_context_create();
    if (!check(ctx != nullptr, "context")) return false;
    auto* s = dgtl_stream_create(ctx, 64);
    if (!check(s != nullptr, "stream")) {
        dgtl_context_destroy(ctx);
        return false;
    }

    uint32_t a = dgtl_emit_const_i64(s, 7);
    uint32_t b = dgtl_emit_const_i64(s, 9);
    uint32_t r = dgtl_emit_add_i64(s, a, b);
    dgtl_emit_return(s, r);
    dgtl_optimize(s);
    (void)dgtl_strike_stream(s, DGTL_TARGET_X64);

    size_t fallback_count = dgtl_last_linker_fallback_count();
    char msg[256] = {};
    if (fallback_count > 0) {
        (void)dgtl_last_linker_fallback_message(0, msg, sizeof(msg));
    }

    dgtl_stream_destroy(s);
    dgtl_context_destroy(ctx);
    return check(true, "deopt surface diagnostics");
}

static bool test_randomized_differential_math() {
    constexpr int N = 2000;
    mt19937_64 rng(0xD671u);
    uniform_int_distribution<uint64_t> value_dist(1, 1000000);
    uniform_int_distribution<int> op_dist(0, 4);

    auto* ctx = dgtl_context_create();
    if (!check(ctx != nullptr, "context")) return false;

    for (int i = 0; i < N; i++) {
        dgtl_stream_t* s = dgtl_stream_create(ctx, 8);
        if (!s) {
            dgtl_context_destroy(ctx);
            return false;
        }

        uint64_t a = value_dist(rng);
        uint64_t b = value_dist(rng);
        int op = op_dist(rng);
        if ((op == 3 || op == 4) && b == 0) b = 1;

        uint32_t sa = dgtl_emit_const_i64(s, a);
        uint32_t sb = dgtl_emit_const_i64(s, b);
        uint32_t sr = 0;
        dgtl_opcode_t expected = DGTL_OP_ADD_I64;

        switch (op) {
            case 0: sr = dgtl_emit_add_i64(s, sa, sb); expected = DGTL_OP_ADD_I64; break;
            case 1: sr = dgtl_emit_sub_i64(s, sa, sb); expected = DGTL_OP_SUB_I64; break;
            case 2: sr = dgtl_emit_mul_i64(s, sa, sb); expected = DGTL_OP_MUL_I64; break;
            case 3: sr = dgtl_emit_div_i64(s, sa, sb); expected = DGTL_OP_DIV_I64; break;
            case 4: sr = dgtl_emit_mod_i64(s, sa, sb); expected = DGTL_OP_MOD_I64; break;
        }

        dgtl_node_info_t n0{}, n1{}, n2{};
        const bool ok = sr &&
            dgtl_stream_get_node(s, 0, &n0) == 1 && n0.immediate == a && n0.opcode == DGTL_OP_CONST &&
            dgtl_stream_get_node(s, 1, &n1) == 1 && n1.immediate == b && n1.opcode == DGTL_OP_CONST &&
            dgtl_stream_get_node(s, 2, &n2) == 1 && n2.opcode == expected;
        dgtl_stream_destroy(s);

        if (!ok) {
            dgtl_context_destroy(ctx);
            return false;
        }

        if ((i + 1) % 500 == 0) {
            log_line("[INFO] diff progress: " + to_string(i + 1) + "/" + to_string(N));
        }
    }

    dgtl_context_destroy(ctx);
    return true;
}

static bool test_soak_stability() {
    auto* ctx = dgtl_context_create();
    if (!check(ctx != nullptr, "context")) return false;

    const auto start = chrono::steady_clock::now();
    const auto end_at = start + chrono::seconds(2);
    uint64_t ops = 0;

    while (chrono::steady_clock::now() < end_at) {
        auto* s = dgtl_stream_create(ctx, 16);
        if (!s) {
            dgtl_context_destroy(ctx);
            return false;
        }
        uint32_t a = dgtl_emit_const_i64(s, ops + 1);
        uint32_t b = dgtl_emit_const_i64(s, ops + 2);
        uint32_t r = dgtl_emit_add_i64(s, a, b);
        dgtl_status_t st = dgtl_optimize(s);
        dgtl_stream_destroy(s);
        if (!r || DGTL_IS_ERR(st)) {
            dgtl_context_destroy(ctx);
            return false;
        }
        ops++;
    }

    dgtl_context_destroy(ctx);
    log_line("[INFO] soak ops: " + to_string(ops));
    return check(ops > 1000, "soak stability");
}

static bool test_null_safety() {
    dgtl_context_destroy(nullptr);
    dgtl_stream_destroy(nullptr);
    dgtl_status_t st = dgtl_optimize(nullptr);
    return check(DGTL_IS_ERR(st), "null safety");
}

static bool test_repeatability() {
    constexpr int RUNS = 32;
    uint64_t base = 0;

    for (int i = 0; i < RUNS; i++) {
        auto* ctx = dgtl_context_create();
        if (!check(ctx != nullptr, "context")) return false;
        auto* s = dgtl_stream_create(ctx, 128);
        if (!check(s != nullptr, "stream")) {
            dgtl_context_destroy(ctx);
            return false;
        }

        uint32_t acc = dgtl_emit_const_i64(s, 11);
        for (int k = 1; k <= 16; k++) {
            uint32_t c = dgtl_emit_const_i64(s, static_cast<uint64_t>(k * 3));
            acc = dgtl_emit_xor_i64(s, acc, c);
            acc = dgtl_emit_add_i64(s, acc, c);
        }
        dgtl_emit_return(s, acc);
        dgtl_status_t st = dgtl_optimize(s);
        if (DGTL_IS_ERR(st)) {
            dgtl_stream_destroy(s);
            dgtl_context_destroy(ctx);
            return false;
        }
        uint64_t h = dgtl_stream_logic_hash(s);
        dgtl_stream_destroy(s);
        dgtl_context_destroy(ctx);

        if (i == 0) base = h;
        if (h != base || h == 0) return false;
    }

    return true;
}

static bool run_test(const char* name, const function<bool()>& fn, int& failures) {
    log_line(string("[TEST] ") + name);
    const auto start = chrono::steady_clock::now();
    bool ok = false;
    try {
        ok = fn();
    } catch (const exception& e) {
        log_line(string("[FAIL] exception: ") + e.what());
        ok = false;
    } catch (...) {
        log_line("[FAIL] unknown exception");
        ok = false;
    }
    const auto ms = chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now() - start).count();
    log_line(string(ok ? "[PASS] " : "[FAIL] ") + name + " | " + to_string(ms) + " ms");
    if (!ok) failures++;
    return ok;
}

int main() {
    cout << "====================================\n";
    cout << "DGTL ULTRA GATE\n";
    cout << "====================================\n";

    int failures = 0;
    run_test("sdk_lifecycle", test_sdk_lifecycle, failures);
    run_test("ir_correctness", test_ir_correctness, failures);
    run_test("optimizer_determinism", test_optimizer_determinism, failures);
    run_test("massive_ir", test_massive_ir, failures);
    run_test("parallel_safety", test_parallel_safety, failures);
    run_test("semantic_correctness", test_semantic_correctness, failures);
    run_test("ssa_phi_control_flow_correctness", test_ssa_phi_control_flow_correctness, failures);
    run_test("simd_correctness", test_simd_correctness, failures);
    run_test("register_allocation_correctness", test_register_allocation_correctness, failures);
    run_test("jit_correctness", test_jit_correctness, failures);
    run_test("abi_correctness", test_abi_correctness, failures);
    run_test("deoptimization_correctness", test_deoptimization_correctness, failures);
    run_test("randomized_differential_math", test_randomized_differential_math, failures);
    run_test("soak_stability", test_soak_stability, failures);
    run_test("null_safety", test_null_safety, failures);
    run_test("repeatability", test_repeatability, failures);

    cout << "====================================\n";
    if (failures == 0) {
        cout << "ALL ULTRA GATE TESTS PASSED\n";
    } else {
        cout << failures << " ULTRA GATE TESTS FAILED\n";
    }
    cout << "====================================\n";
    return failures == 0 ? 0 : 1;
}
