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

#include "dgtl_sdk/dgtl.h"

#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#define REQUIRE(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] " << (msg) << " at line " << __LINE__ << std::endl; \
            return false; \
        } \
    } while (0)

#define PASS(msg) \
    do { \
        std::cout << "[PASS] " << (msg) << std::endl; \
    } while (0)

static bool test_version_and_capabilities() {
    dgtl_version_info_t v = dgtl_get_version();
    REQUIRE(v.major >= 2, "Major version >= 2");
    REQUIRE(v.build_id != nullptr, "Build ID not null");

    const char* cfg = dgtl_get_build_config();
    REQUIRE(cfg != nullptr && cfg[0] != '\0', "Build config present");

    dgtl_capabilities_t caps = dgtl_query_capabilities();
    REQUIRE(caps.core_count >= 1, "Core count >= 1");

    PASS("Version & Capabilities Query");
    return true;
}

static bool test_context_and_stream_lifecycle() {
    dgtl_context_t* ctx = dgtl_context_create(2, 64);
    REQUIRE(ctx != nullptr, "Context creation");

    dgtl_stream_t* stream = dgtl_stream_create(ctx, 256);
    REQUIRE(stream != nullptr, "Stream creation");
    REQUIRE(dgtl_stream_size(stream) == 0, "Stream starts empty");

    dgtl_stream_destroy(stream);
    dgtl_context_destroy(ctx);

    PASS("Context & Stream Lifecycle");
    return true;
}

static bool test_scalar_arithmetic_pipeline() {
    dgtl_context_t* ctx = dgtl_context_create(1, 32);
    REQUIRE(ctx != nullptr, "Context creation");

    dgtl_stream_t* stream = dgtl_stream_create(ctx, 128);
    REQUIRE(stream != nullptr, "Stream creation");

    dgtl_func_builder_t* fb = dgtl_func_builder_create(stream, "scalar_test", DGTL_TYPE_I64);
    REQUIRE(fb != nullptr, "Func builder creation");

    dgtl_block_t entry = dgtl_func_append_block(fb, "entry");
    dgtl_func_set_insert_block(fb, entry);

    dgtl_value_t a = dgtl_build_const_i64(fb, 100);
    dgtl_value_t b = dgtl_build_const_i64(fb, 25);
    dgtl_value_t sum = dgtl_build_add(fb, a, b);
    dgtl_value_t diff = dgtl_build_sub(fb, sum, b);
    dgtl_value_t prod = dgtl_build_mul(fb, diff, b);
    dgtl_value_t quot = dgtl_build_div(fb, prod, b);
    dgtl_value_t rem = dgtl_build_rem(fb, quot, b);
    dgtl_build_ret(fb, rem);

    (void)dgtl_func_builder_finalize(fb);

    REQUIRE(dgtl_value_is_valid(sum), "Add valid");
    REQUIRE(dgtl_value_is_valid(diff), "Sub valid");
    REQUIRE(dgtl_value_is_valid(prod), "Mul valid");
    REQUIRE(dgtl_value_is_valid(quot), "Div valid");
    REQUIRE(dgtl_value_is_valid(rem), "Rem valid");

    dgtl_err_t opt_err = dgtl_optimize_with_profile(stream, DGTL_OPT_O2);
    REQUIRE(DGTL_IS_OK(opt_err), "O2 Optimization");

    dgtl_strike_result_t strike = dgtl_strike_stream(stream, DGTL_TARGET_X86_64);
    REQUIRE(strike.ok == 1, "x86_64 target strike");
    REQUIRE(strike.code_size > 0, "Emitted code > 0");

    dgtl_func_builder_destroy(fb);
    dgtl_stream_destroy(stream);
    dgtl_context_destroy(ctx);

    PASS("Scalar Arithmetic Pipeline");
    return true;
}

static bool test_control_flow_and_phi() {
    dgtl_context_t* ctx = dgtl_context_create(1, 32);
    REQUIRE(ctx != nullptr, "Context creation");

    dgtl_stream_t* stream = dgtl_stream_create(ctx, 128);
    REQUIRE(stream != nullptr, "Stream creation");

    dgtl_func_builder_t* fb = dgtl_func_builder_create(stream, "cfg_phi_test", DGTL_TYPE_I64);
    REQUIRE(fb != nullptr, "Func builder creation");

    dgtl_block_t b_entry = dgtl_func_append_block(fb, "entry");
    dgtl_block_t b_then  = dgtl_func_append_block(fb, "then");
    dgtl_block_t b_else  = dgtl_func_append_block(fb, "else");
    dgtl_block_t b_join  = dgtl_func_append_block(fb, "join");

    dgtl_func_set_insert_block(fb, b_entry);
    dgtl_value_t x = dgtl_build_const_i64(fb, 42);
    dgtl_value_t y = dgtl_build_const_i64(fb, 50);
    dgtl_value_t cmp = dgtl_build_icmp(fb, DGTL_ICMP_SLT, x, y);
    dgtl_build_cond_br(fb, cmp, b_then, b_else);

    dgtl_func_set_insert_block(fb, b_then);
    dgtl_value_t val_then = dgtl_build_const_i64(fb, 100);
    dgtl_build_br(fb, b_join);

    dgtl_func_set_insert_block(fb, b_else);
    dgtl_value_t val_else = dgtl_build_const_i64(fb, 200);
    dgtl_build_br(fb, b_join);

    dgtl_func_set_insert_block(fb, b_join);
    dgtl_value_t phi = dgtl_build_phi(fb, DGTL_TYPE_I64);
    dgtl_phi_add_incoming(fb, phi, val_then, b_then);
    dgtl_phi_add_incoming(fb, phi, val_else, b_else);
    dgtl_build_ret(fb, phi);

    (void)dgtl_func_builder_finalize(fb);

    dgtl_err_t opt_err = dgtl_optimize_with_profile(stream, DGTL_OPT_O2);
    REQUIRE(DGTL_IS_OK(opt_err), "Optimize CFG & Phi");

    dgtl_func_builder_destroy(fb);
    dgtl_stream_destroy(stream);
    dgtl_context_destroy(ctx);

    PASS("Control Flow & Phi Resolution");
    return true;
}

static bool test_memory_alloca_load_store() {
    dgtl_context_t* ctx = dgtl_context_create(1, 32);
    REQUIRE(ctx != nullptr, "Context creation");

    dgtl_stream_t* stream = dgtl_stream_create(ctx, 128);
    REQUIRE(stream != nullptr, "Stream creation");

    dgtl_func_builder_t* fb = dgtl_func_builder_create(stream, "mem_test", DGTL_TYPE_I64);
    REQUIRE(fb != nullptr, "Func builder creation");

    dgtl_block_t entry = dgtl_func_append_block(fb, "entry");
    dgtl_func_set_insert_block(fb, entry);

    dgtl_value_t slot = dgtl_build_alloca(fb, DGTL_TYPE_I64, 8);
    REQUIRE(dgtl_value_is_valid(slot), "Alloca slot valid");

    dgtl_value_t val = dgtl_build_const_i64(fb, 777);
    dgtl_err_t store_err = dgtl_build_store(fb, val, slot);
    REQUIRE(DGTL_IS_OK(store_err), "Store valid");

    dgtl_value_t loaded = dgtl_build_load(fb, DGTL_TYPE_I64, slot);
    REQUIRE(dgtl_value_is_valid(loaded), "Loaded value valid");
    dgtl_build_ret(fb, loaded);

    (void)dgtl_func_builder_finalize(fb);

    dgtl_err_t opt_err = dgtl_optimize_with_profile(stream, DGTL_OPT_O1);
    REQUIRE(DGTL_IS_OK(opt_err), "Optimize memory ops");

    dgtl_func_builder_destroy(fb);
    dgtl_stream_destroy(stream);
    dgtl_context_destroy(ctx);

    PASS("Memory Alloca/Load/Store");
    return true;
}

static bool test_multithreaded_pipeline_concurrency() {
    constexpr int NUM_THREADS = 4;
    constexpr int ITERS_PER_THREAD = 100;
    std::vector<std::thread> threads;
    std::atomic<int> errors{0};

    for (int t = 0; t < NUM_THREADS; t++) {
        threads.emplace_back([&errors, t]() {
            for (int i = 0; i < ITERS_PER_THREAD; i++) {
                dgtl_context_t* ctx = dgtl_context_create(1, 16);
                if (!ctx) { errors.fetch_add(1); return; }

                dgtl_stream_t* stream = dgtl_stream_create(ctx, 64);
                if (!stream) { dgtl_context_destroy(ctx); errors.fetch_add(1); return; }

                dgtl_func_builder_t* fb = dgtl_func_builder_create(stream, "mt_fn", DGTL_TYPE_I64);
                if (fb) {
                    dgtl_block_t b = dgtl_func_append_block(fb, "entry");
                    dgtl_func_set_insert_block(fb, b);
                    dgtl_value_t v1 = dgtl_build_const_i64(fb, t + i);
                    dgtl_value_t v2 = dgtl_build_const_i64(fb, 10);
                    dgtl_value_t sum = dgtl_build_add(fb, v1, v2);
                    dgtl_build_ret(fb, sum);
                    (void)dgtl_func_builder_finalize(fb);
                    dgtl_func_builder_destroy(fb);
                }

                dgtl_err_t opt_err = dgtl_optimize(stream);
                if (DGTL_IS_ERR(opt_err)) errors.fetch_add(1);

                dgtl_stream_destroy(stream);
                dgtl_context_destroy(ctx);
            }
        });
    }

    for (auto& th : threads) th.join();
    REQUIRE(errors.load() == 0, "No errors in multithreaded pipeline");

    PASS("Multithreaded Pipeline Concurrency");
    return true;
}

static bool test_null_safety_and_invalid_handles() {
    dgtl_context_destroy(nullptr);
    dgtl_stream_destroy(nullptr);
    dgtl_func_builder_destroy(nullptr);

    dgtl_err_t err = dgtl_optimize(nullptr);
    REQUIRE(DGTL_IS_ERR(err), "Optimize nullptr returns error");

    dgtl_strike_result_t strike = dgtl_strike_stream(nullptr, DGTL_TARGET_X86_64);
    REQUIRE(strike.ok == 0, "Strike nullptr returns not ok");

    PASS("Null Safety & Invalid Handles");
    return true;
}

int main() {
    std::cout << "========================================================\n";
    std::cout << "  DGTL ULTIMATE SDK SUITE (Complete C-API Conformance)  \n";
    std::cout << "========================================================\n";

    int failures = 0;
    if (!test_version_and_capabilities()) failures++;
    if (!test_context_and_stream_lifecycle()) failures++;
    if (!test_scalar_arithmetic_pipeline()) failures++;
    if (!test_control_flow_and_phi()) failures++;
    if (!test_memory_alloca_load_store()) failures++;
    if (!test_multithreaded_pipeline_concurrency()) failures++;
    if (!test_null_safety_and_invalid_handles()) failures++;

    std::cout << "========================================================\n";
    if (failures == 0) {
        std::cout << "  ALL ULTIMATE SDK TESTS PASSED (0 failures)            \n";
    } else {
        std::cout << "  FAILED: " << failures << " tests failed!              \n";
    }
    std::cout << "========================================================\n";

    return failures == 0 ? 0 : 1;
}
