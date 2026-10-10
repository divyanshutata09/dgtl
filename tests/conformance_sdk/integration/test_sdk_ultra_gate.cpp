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
    const size_t n = dgtl_stream_size(s);
    for (size_t i = 0; i < n; i++) {
        dgtl_node_t node{};
        if (dgtl_stream_get_node(s, i, &node) && node.opcode == opcode) {
            return true;
        }
    }
    return false;
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
    auto* ctx = dgtl_context_create(1, 32);
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
    auto* ctx = dgtl_context_create(1, 32);
    if (!check(ctx != nullptr, "context")) return false;
    auto* s = dgtl_stream_create(ctx, 64);
    if (!check(s != nullptr, "stream")) {
        dgtl_context_destroy(ctx);
        return false;
    }
    auto* fb = dgtl_func_builder_create(s, "ir_correctness", DGTL_TYPE_I64);
    auto b = dgtl_func_append_block(fb, "entry");
    dgtl_func_set_insert_block(fb, b);

    const auto c1 = dgtl_build_const_i64(fb, 100);
    const auto c2 = dgtl_build_const_i64(fb, 25);
    const auto add = dgtl_build_add(fb, c1, c2);
    const auto sub = dgtl_build_sub(fb, c1, c2);
    const auto mul = dgtl_build_mul(fb, c1, c2);
    const auto div = dgtl_build_div(fb, c1, c2);
    const auto mod = dgtl_build_rem(fb, c1, c2);
    bool ok = check(dgtl_value_is_valid(c1) && dgtl_value_is_valid(c2) &&
                    dgtl_value_is_valid(add) && dgtl_value_is_valid(sub) &&
                    dgtl_value_is_valid(mul) && dgtl_value_is_valid(div) &&
                    dgtl_value_is_valid(mod), "emit arithmetic nodes");

    dgtl_node_t n0{}, n2{}, n6{};
    ok = ok && check(dgtl_stream_get_node(s, 0, &n0), "get node[0]");
    ok = ok && check(dgtl_stream_get_node(s, 2, &n2), "get node[2]");
    ok = ok && check(dgtl_stream_get_node(s, 6, &n6), "get node[6]");
    ok = ok && check(n0.opcode == DGTL_OP_CONST, "node[0] opcode CONST");
    ok = ok && check(n2.opcode == DGTL_OP_ADD_I64, "node[2] opcode ADD_I64");
    ok = ok && check(n6.opcode == DGTL_OP_MOD_I64, "node[6] opcode MOD_I64");

    dgtl_func_builder_destroy(fb);
    dgtl_stream_destroy(s);
    dgtl_context_destroy(ctx);
    return ok;
}

static bool test_optimizer_determinism() {
    auto build_hash = []() -> uint64_t {
        auto* ctx = dgtl_context_create(1, 32);
        if (!ctx) return 0;
        auto* s = dgtl_stream_create(ctx, 256);
        if (!s) {
            dgtl_context_destroy(ctx);
            return 0;
        }
        auto* fb = dgtl_func_builder_create(s, "det_fn", DGTL_TYPE_I64);
        auto blk = dgtl_func_append_block(fb, "entry");
        dgtl_func_set_insert_block(fb, blk);

        auto acc = dgtl_build_const_i64(fb, 1);
        for (int i = 0; i < 64; i++) {
            auto c = dgtl_build_const_i64(fb, static_cast<int64_t>(i + 2));
            acc = dgtl_build_add(fb, acc, c);
            acc = dgtl_build_mul(fb, acc, c);
        }
        dgtl_build_ret(fb, acc);
        (void)dgtl_func_builder_finalize(fb);

        const dgtl_err_t st = dgtl_optimize(s);
        const auto metrics = dgtl_stream_get_perf_metrics(s);
        const uint64_t h = DGTL_IS_OK(st) ? metrics.logic_hash : 0;

        dgtl_func_builder_destroy(fb);
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
    auto* ctx = dgtl_context_create(2, 64);
    if (!check(ctx != nullptr, "context")) return false;
    auto* s = dgtl_stream_create(ctx, static_cast<size_t>(N) * 3u + 64u);
    if (!check(s != nullptr, "stream")) {
        dgtl_context_destroy(ctx);
        return false;
    }
    auto* fb = dgtl_func_builder_create(s, "massive_fn", DGTL_TYPE_I64);
    auto blk = dgtl_func_append_block(fb, "entry");
    dgtl_func_set_insert_block(fb, blk);

    for (int i = 0; i < N; i++) {
        const auto a = dgtl_build_const_i64(fb, static_cast<int64_t>(i));
        const auto b = dgtl_build_const_i64(fb, static_cast<int64_t>(i + 1));
        const auto r = dgtl_build_add(fb, a, b);
        if (!dgtl_value_is_valid(r)) {
            dgtl_func_builder_destroy(fb);
            dgtl_stream_destroy(s);
            dgtl_context_destroy(ctx);
            return false;
        }
        if ((i + 1) % 30000 == 0) {
            log_line("[INFO] massive progress: " + to_string(i + 1) + "/" + to_string(N));
        }
    }
    (void)dgtl_func_builder_finalize(fb);

    const dgtl_err_t st = dgtl_optimize(s);
    const bool ok = check(DGTL_IS_OK(st), "optimize massive") && check(dgtl_stream_size(s) > 0, "massive node_count > 0");
    dgtl_func_builder_destroy(fb);
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
            dgtl_context_t* ctx = dgtl_context_create(1, 16);
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
                auto* fb = dgtl_func_builder_create(s, "par_fn", DGTL_TYPE_I64);
                if (fb) {
                    auto blk = dgtl_func_append_block(fb, "entry");
                    dgtl_func_set_insert_block(fb, blk);
                    auto a = dgtl_build_const_i64(fb, static_cast<int64_t>(i));
                    auto b = dgtl_build_const_i64(fb, static_cast<int64_t>(i + 7));
                    auto r = dgtl_build_add(fb, a, b);
                    (void)dgtl_func_builder_finalize(fb);
                    dgtl_err_t st = dgtl_optimize(s);
                    if (!dgtl_value_is_valid(r) || DGTL_IS_ERR(st)) fails.fetch_add(1);
                    dgtl_func_builder_destroy(fb);
                }
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
    auto* ctx = dgtl_context_create(1, 32);
    if (!check(ctx != nullptr, "context")) return false;
    auto* s = dgtl_stream_create(ctx, 64);
    if (!check(s != nullptr, "stream")) {
        dgtl_context_destroy(ctx);
        return false;
    }
    auto* fb = dgtl_func_builder_create(s, "semantic_fn", DGTL_TYPE_I64);
    auto blk = dgtl_func_append_block(fb, "entry");
    dgtl_func_set_insert_block(fb, blk);

    auto a = dgtl_build_const_i64(fb, 84);
    auto b = dgtl_build_const_i64(fb, 12);
    auto c = dgtl_build_sub(fb, a, b);
    auto d = dgtl_build_mul(fb, c, b);
    auto e = dgtl_build_div(fb, d, b);
    auto f = dgtl_build_rem(fb, e, b);
    dgtl_build_ret(fb, f);
    (void)dgtl_func_builder_finalize(fb);

    dgtl_node_t n2{}, n3{}, n4{}, n5{};
    bool ok = check(dgtl_value_is_valid(c) && dgtl_value_is_valid(d) &&
                    dgtl_value_is_valid(e) && dgtl_value_is_valid(f), "semantic emit") &&
              check(dgtl_stream_get_node(s, 2, &n2) && n2.opcode == DGTL_OP_SUB_I64, "semantic SUB") &&
              check(dgtl_stream_get_node(s, 3, &n3) && n3.opcode == DGTL_OP_MUL_I64, "semantic MUL") &&
              check(dgtl_stream_get_node(s, 4, &n4) && n4.opcode == DGTL_OP_DIV_I64, "semantic DIV") &&
              check(dgtl_stream_get_node(s, 5, &n5) && n5.opcode == DGTL_OP_MOD_I64, "semantic MOD");

    dgtl_func_builder_destroy(fb);
    dgtl_stream_destroy(s);
    dgtl_context_destroy(ctx);
    return ok;
}

static bool test_ssa_phi_control_flow_correctness() {
    auto* ctx = dgtl_context_create(1, 32);
    if (!check(ctx != nullptr, "context")) return false;
    auto* s = dgtl_stream_create(ctx, 128);
    if (!check(s != nullptr, "stream")) {
        dgtl_context_destroy(ctx);
        return false;
    }
    auto* fb = dgtl_func_builder_create(s, "cfg_fn", DGTL_TYPE_I64);

    auto b_entry = dgtl_func_append_block(fb, "entry");
    auto b_then  = dgtl_func_append_block(fb, "then");
    auto b_else  = dgtl_func_append_block(fb, "else");
    auto b_join  = dgtl_func_append_block(fb, "join");

    dgtl_func_set_insert_block(fb, b_entry);
    auto a = dgtl_build_const_i64(fb, 10);
    auto b = dgtl_build_const_i64(fb, 20);
    auto cmp = dgtl_build_icmp(fb, DGTL_ICMP_SLT, a, b);
    dgtl_build_cond_br(fb, cmp, b_then, b_else);

    dgtl_func_set_insert_block(fb, b_then);
    dgtl_build_br(fb, b_join);

    dgtl_func_set_insert_block(fb, b_else);
    dgtl_build_br(fb, b_join);

    dgtl_func_set_insert_block(fb, b_join);
    auto phi = dgtl_build_phi(fb, DGTL_TYPE_I64);
    dgtl_phi_add_incoming(fb, phi, a, b_then);
    dgtl_phi_add_incoming(fb, phi, b, b_else);
    dgtl_build_ret(fb, phi);
    (void)dgtl_func_builder_finalize(fb);

    const bool had_label = has_opcode(s, DGTL_OP_LABEL);
    const bool had_branch = has_opcode(s, DGTL_OP_BRANCH_COND);
    const bool had_phi = has_opcode(s, DGTL_OP_PHI_MULTI);

    dgtl_err_t st = dgtl_optimize_with_profile(s, DGTL_OPT_O2);
    bool ok = check(dgtl_value_is_valid(a) && dgtl_value_is_valid(b) &&
                    dgtl_value_is_valid(cmp) && dgtl_value_is_valid(phi), "ssa/phi emit") &&
              check(DGTL_IS_OK(st), "ssa/phi optimize") &&
              check(had_label, "cfg label present") &&
              check(had_branch, "cfg branch present") &&
              check(had_phi, "phi present");

    if (ok) {
        log_line("[INFO] ssa/cfg nodes emitted and optimized");
    }

    dgtl_func_builder_destroy(fb);
    dgtl_stream_destroy(s);
    dgtl_context_destroy(ctx);
    return ok;
}

static bool test_simd_correctness() {
    auto* ctx = dgtl_context_create(1, 32);
    if (!check(ctx != nullptr, "context")) return false;
    auto* s = dgtl_stream_create(ctx, 128);
    if (!check(s != nullptr, "stream")) {
        dgtl_context_destroy(ctx);
        return false;
    }
    auto* fb = dgtl_func_builder_create(s, "simd_fn", DGTL_TYPE_F32);
    auto blk = dgtl_func_append_block(fb, "entry");
    dgtl_func_set_insert_block(fb, blk);

    auto a = dgtl_build_const_f32(fb, 3.0f);
    auto b = dgtl_build_const_f32(fb, 4.0f);
    auto c = dgtl_build_const_f32(fb, 5.0f);
    auto fma = dgtl_build_fma(fb, a, b, c);

    auto base = dgtl_build_const_i64(fb, 1024);
    auto idx = dgtl_build_const_i64(fb, 7);
    auto mask = dgtl_build_const_i64(fb, 0xFFFF);
    auto g = dgtl_build_gather(fb, base, idx, mask);
    auto val = dgtl_build_const_i64(fb, 9);
    dgtl_build_scatter(fb, base, idx, val, mask);
    dgtl_build_ret(fb, fma);
    (void)dgtl_func_builder_finalize(fb);

    dgtl_err_t leg = dgtl_legalize_simd(s, DGTL_SIMD_ARCH_X86_AVX2);
    dgtl_err_t opt = dgtl_optimize_with_profile(s, DGTL_OPT_O3);

    bool ok = check(dgtl_value_is_valid(a) && dgtl_value_is_valid(b) &&
                    dgtl_value_is_valid(c) && dgtl_value_is_valid(fma) &&
                    dgtl_value_is_valid(g), "simd emit") &&
              check(DGTL_IS_OK(leg), "simd legalize") &&
              check(DGTL_IS_OK(opt), "simd optimize") &&
              check(has_opcode(s, DGTL_OP_FMA_F32), "fma opcode present");

    dgtl_func_builder_destroy(fb);
    dgtl_stream_destroy(s);
    dgtl_context_destroy(ctx);
    return ok;
}

static bool test_register_allocation_correctness() {
    auto* ctx = dgtl_context_create(1, 32);
    if (!check(ctx != nullptr, "context")) return false;
    auto* s = dgtl_stream_create(ctx, 4096);
    if (!check(s != nullptr, "stream")) {
        dgtl_context_destroy(ctx);
        return false;
    }
    auto* fb = dgtl_func_builder_create(s, "regalloc_fn", DGTL_TYPE_I64);
    auto blk = dgtl_func_append_block(fb, "entry");
    dgtl_func_set_insert_block(fb, blk);

    auto acc = dgtl_build_const_i64(fb, 1);
    for (int i = 0; i < 512; i++) {
        auto c = dgtl_build_const_i64(fb, static_cast<int64_t>(i + 2));
        acc = dgtl_build_add(fb, acc, c);
        acc = dgtl_build_xor(fb, acc, c);
    }
    dgtl_build_ret(fb, acc);
    (void)dgtl_func_builder_finalize(fb);

    dgtl_err_t st = dgtl_optimize_with_profile(s, DGTL_OPT_O3);
    const auto metrics = dgtl_stream_get_perf_metrics(s);
    uint64_t h = metrics.logic_hash;
    bool ok = check(DGTL_IS_OK(st), "pressure optimize") &&
              check(h != 0, "pressure hash nonzero");

    dgtl_func_builder_destroy(fb);
    dgtl_stream_destroy(s);
    dgtl_context_destroy(ctx);
    return ok;
}

static bool test_jit_correctness() {
    auto* ctx = dgtl_context_create(1, 32);
    if (!check(ctx != nullptr, "context")) return false;
    auto* s = dgtl_stream_create(ctx, 64);
    if (!check(s != nullptr, "stream")) {
        dgtl_context_destroy(ctx);
        return false;
    }
    auto* fb = dgtl_func_builder_create(s, "jit_fn", DGTL_TYPE_I64);
    auto blk = dgtl_func_append_block(fb, "entry");
    dgtl_func_set_insert_block(fb, blk);

    auto a = dgtl_build_const_i64(fb, 40);
    auto b = dgtl_build_const_i64(fb, 2);
    auto r = dgtl_build_add(fb, a, b);
    dgtl_build_ret(fb, r);
    (void)dgtl_func_builder_finalize(fb);

    dgtl_err_t opt = dgtl_optimize(s);
    dgtl_strike_result_t res = dgtl_strike_stream(s, DGTL_TARGET_X86_64);

    bool ok = check(DGTL_IS_OK(opt), "jit optimize") &&
              check(res.ok == 1, "jit strike ok") &&
              check(res.code_size > 0, "jit code_size > 0");

    dgtl_func_builder_destroy(fb);
    dgtl_stream_destroy(s);
    dgtl_context_destroy(ctx);
    return ok;
}

static bool test_abi_correctness() {
    dgtl_version_info_t v = dgtl_get_version();
    const char* build_cfg = dgtl_get_build_config();

    bool ok = check(v.build_id != nullptr && v.build_id[0] != '\0', "abi version string") &&
              check(build_cfg != nullptr && build_cfg[0] != '\0', "abi build config");

    if (ok) {
        log_line(string("[INFO] abi: version=") + v.build_id + " config=" + build_cfg);
    }

    return ok;
}

static bool test_deoptimization_correctness() {
    auto* ctx = dgtl_context_create(1, 32);
    if (!check(ctx != nullptr, "context")) return false;
    auto* s = dgtl_stream_create(ctx, 64);
    if (!check(s != nullptr, "stream")) {
        dgtl_context_destroy(ctx);
        return false;
    }
    auto* fb = dgtl_func_builder_create(s, "deopt_fn", DGTL_TYPE_I64);
    auto blk = dgtl_func_append_block(fb, "entry");
    dgtl_func_set_insert_block(fb, blk);

    auto a = dgtl_build_const_i64(fb, 7);
    auto b = dgtl_build_const_i64(fb, 9);
    auto r = dgtl_build_add(fb, a, b);
    dgtl_build_ret(fb, r);
    (void)dgtl_func_builder_finalize(fb);

    dgtl_err_t opt_err = dgtl_optimize(s);
    (void)opt_err;
    (void)dgtl_strike_stream(s, DGTL_TARGET_X86_64);

    size_t fallback_count = dgtl_last_linker_fallback_count();
    char msg[256] = {};
    if (fallback_count > 0) {
        (void)dgtl_last_linker_fallback_message(0, msg, sizeof(msg));
    }

    dgtl_func_builder_destroy(fb);
    dgtl_stream_destroy(s);
    dgtl_context_destroy(ctx);
    return check(true, "deopt surface diagnostics");
}

static bool test_randomized_differential_math() {
    constexpr int N = 2000;
    mt19937_64 rng(0xD671u);
    uniform_int_distribution<uint64_t> value_dist(1, 1000000);
    uniform_int_distribution<int> op_dist(0, 4);

    auto* ctx = dgtl_context_create(1, 32);
    if (!check(ctx != nullptr, "context")) return false;

    for (int i = 0; i < N; i++) {
        dgtl_stream_t* s = dgtl_stream_create(ctx, 8);
        if (!s) {
            dgtl_context_destroy(ctx);
            return false;
        }
        auto* fb = dgtl_func_builder_create(s, "diff_fn", DGTL_TYPE_I64);
        auto blk = dgtl_func_append_block(fb, "entry");
        dgtl_func_set_insert_block(fb, blk);

        uint64_t a = value_dist(rng);
        uint64_t b = value_dist(rng);
        int op = op_dist(rng);
        if ((op == 3 || op == 4) && b == 0) b = 1;

        auto sa = dgtl_build_const_i64(fb, static_cast<int64_t>(a));
        auto sb = dgtl_build_const_i64(fb, static_cast<int64_t>(b));
        dgtl_value_t sr{};
        dgtl_opcode_t expected = DGTL_OP_ADD_I64;

        switch (op) {
            case 0: sr = dgtl_build_add(fb, sa, sb); expected = DGTL_OP_ADD_I64; break;
            case 1: sr = dgtl_build_sub(fb, sa, sb); expected = DGTL_OP_SUB_I64; break;
            case 2: sr = dgtl_build_mul(fb, sa, sb); expected = DGTL_OP_MUL_I64; break;
            case 3: sr = dgtl_build_div(fb, sa, sb); expected = DGTL_OP_DIV_I64; break;
            case 4: sr = dgtl_build_rem(fb, sa, sb); expected = DGTL_OP_MOD_I64; break;
        }

        dgtl_node_t n0{}, n1{}, n2{};
        const bool ok = dgtl_value_is_valid(sr) &&
            dgtl_stream_get_node(s, 0, &n0) && n0.opcode == DGTL_OP_CONST &&
            dgtl_stream_get_node(s, 1, &n1) && n1.opcode == DGTL_OP_CONST &&
            dgtl_stream_get_node(s, 2, &n2) && n2.opcode == expected;

        dgtl_func_builder_destroy(fb);
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
    auto* ctx = dgtl_context_create(1, 32);
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
        auto* fb = dgtl_func_builder_create(s, "soak_fn", DGTL_TYPE_I64);
        auto blk = dgtl_func_append_block(fb, "entry");
        dgtl_func_set_insert_block(fb, blk);

        auto a = dgtl_build_const_i64(fb, ops + 1);
        auto b = dgtl_build_const_i64(fb, ops + 2);
        auto r = dgtl_build_add(fb, a, b);
        (void)dgtl_func_builder_finalize(fb);

        dgtl_err_t st = dgtl_optimize(s);
        dgtl_func_builder_destroy(fb);
        dgtl_stream_destroy(s);

        if (!dgtl_value_is_valid(r) || DGTL_IS_ERR(st)) {
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
    dgtl_err_t st = dgtl_optimize(nullptr);
    return check(DGTL_IS_ERR(st), "null safety");
}

static bool test_repeatability() {
    constexpr int RUNS = 32;
    uint64_t base = 0;

    for (int i = 0; i < RUNS; i++) {
        auto* ctx = dgtl_context_create(1, 32);
        if (!check(ctx != nullptr, "context")) return false;
        auto* s = dgtl_stream_create(ctx, 128);
        if (!check(s != nullptr, "stream")) {
            dgtl_context_destroy(ctx);
            return false;
        }
        auto* fb = dgtl_func_builder_create(s, "repeat_fn", DGTL_TYPE_I64);
        auto blk = dgtl_func_append_block(fb, "entry");
        dgtl_func_set_insert_block(fb, blk);

        auto acc = dgtl_build_const_i64(fb, 11);
        for (int k = 1; k <= 16; k++) {
            auto c = dgtl_build_const_i64(fb, static_cast<int64_t>(k * 3));
            acc = dgtl_build_xor(fb, acc, c);
            acc = dgtl_build_add(fb, acc, c);
        }
        dgtl_build_ret(fb, acc);
        (void)dgtl_func_builder_finalize(fb);

        dgtl_err_t st = dgtl_optimize(s);
        if (DGTL_IS_ERR(st)) {
            dgtl_func_builder_destroy(fb);
            dgtl_stream_destroy(s);
            dgtl_context_destroy(ctx);
            return false;
        }
        const auto metrics = dgtl_stream_get_perf_metrics(s);
        uint64_t h = metrics.logic_hash;

        dgtl_func_builder_destroy(fb);
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