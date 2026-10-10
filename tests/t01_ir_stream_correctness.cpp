/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 * Component:   T01 IR Stream Correctness Validation
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

#include "dgtl_sdk/dgtl.h"
#include <cstdio>
#include <cstdlib>

int main() {
    dgtl_context_t* ctx = dgtl_context_create(1, 32);
    if (!ctx) {
        std::fprintf(stderr, "[FAIL] Context creation failed\n");
        return 1;
    }

    dgtl_stream_t* stream = dgtl_stream_create(ctx, 128);
    if (!stream) {
        std::fprintf(stderr, "[FAIL] Stream creation failed\n");
        dgtl_context_destroy(ctx);
        return 1;
    }

    dgtl_func_builder_t* fb = dgtl_func_builder_create(stream, "t01_kernel", DGTL_TYPE_I32);
    if (!fb) {
        std::fprintf(stderr, "[FAIL] Func builder creation failed\n");
        dgtl_stream_destroy(stream);
        dgtl_context_destroy(ctx);
        return 1;
    }

    dgtl_block_t entry = dgtl_func_append_block(fb, "entry");
    dgtl_func_set_insert_block(fb, entry);

    dgtl_value_t a = dgtl_build_const_i32(fb, 10);
    dgtl_value_t b = dgtl_build_const_i32(fb, 20);
    dgtl_value_t sum = dgtl_build_add(fb, a, b);
    dgtl_build_ret(fb, sum);

    dgtl_err_t err = dgtl_func_builder_finalize(fb);
    if (DGTL_IS_ERR(err)) {
        std::fprintf(stderr, "[FAIL] Finalize failed: %s\n", dgtl_err_message(err));
        dgtl_func_builder_destroy(fb);
        dgtl_stream_destroy(stream);
        dgtl_context_destroy(ctx);
        return 1;
    }

    if (dgtl_stream_size(stream) == 0) {
        std::fprintf(stderr, "[FAIL] Stream node count is 0\n");
        dgtl_func_builder_destroy(fb);
        dgtl_stream_destroy(stream);
        dgtl_context_destroy(ctx);
        return 1;
    }

    dgtl_session_config_t cfg = dgtl_session_config_default();
    dgtl_session_t* sess = dgtl_session_create(&cfg);
    if (!sess) {
        std::fprintf(stderr, "[FAIL] Session creation failed\n");
        dgtl_func_builder_destroy(fb);
        dgtl_stream_destroy(stream);
        dgtl_context_destroy(ctx);
        return 1;
    }

    err = dgtl_session_compile(sess, stream);
    if (DGTL_IS_ERR(err)) {
        std::fprintf(stderr, "[FAIL] Compilation failed: %s\n", dgtl_err_message(err));
        dgtl_session_destroy(sess);
        dgtl_func_builder_destroy(fb);
        dgtl_stream_destroy(stream);
        dgtl_context_destroy(ctx);
        return 1;
    }

    int64_t result = 0;
    err = dgtl_session_run_i64(sess, "t01_kernel", nullptr, 0, &result);
    if (DGTL_IS_ERR(err) || result != 30) {
        std::fprintf(stderr, "[FAIL] Execution failed or incorrect result: %lld\n", (long long)result);
        dgtl_session_destroy(sess);
        dgtl_func_builder_destroy(fb);
        dgtl_stream_destroy(stream);
        dgtl_context_destroy(ctx);
        return 1;
    }

    dgtl_session_destroy(sess);
    dgtl_func_builder_destroy(fb);
    dgtl_stream_destroy(stream);
    dgtl_context_destroy(ctx);

    std::printf("[PASS] t01_ir_stream_correctness\n");
    return 0;
}
