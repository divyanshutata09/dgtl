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
#include <cstdio>
#include <cstdlib>
#include <iostream>

int main() {
    std::cout << "--- [DGTL] Pipeline Integration Test ---\n";

    // 1. Context & Stream Setup
    dgtl_context_t* ctx = dgtl_context_create(1, 32);
    if (!ctx) {
        std::cerr << "[FATAL] Failed to create DGTL context\n";
        return 1;
    }

    dgtl_stream_t* stream = dgtl_stream_create(ctx, 1024);
    if (!stream) {
        std::cerr << "[FATAL] Failed to allocate IR stream\n";
        dgtl_context_destroy(ctx);
        return 1;
    }

    // 2. Build IR Function
    dgtl_func_builder_t* fb = dgtl_func_builder_create(stream, "pipeline_kernel", DGTL_TYPE_I64);
    if (!fb) {
        std::cerr << "[FATAL] Failed to create func builder\n";
        dgtl_stream_destroy(stream);
        dgtl_context_destroy(ctx);
        return 1;
    }

    dgtl_block_t entry = dgtl_func_append_block(fb, "entry");
    dgtl_func_set_insert_block(fb, entry);

    // Node 0: a = 10
    dgtl_value_t a = dgtl_build_const_i64(fb, 10);
    // Node 1: b = 11
    dgtl_value_t b = dgtl_build_const_i64(fb, 11);
    // Node 2: c = a + b
    dgtl_value_t c = dgtl_build_add(fb, a, b);
    // Node 3: d = a + b (redundant for GVN testing)
    dgtl_value_t d = dgtl_build_add(fb, a, b);
    // Node 4: ret c + d
    dgtl_value_t res = dgtl_build_add(fb, c, d);
    dgtl_build_ret(fb, res);

    (void)dgtl_func_builder_finalize(fb);

    const size_t initial_nodes = dgtl_stream_size(stream);
    std::cout << "[Step 1] IR Injected. Initial Count: " << initial_nodes << "\n";

    // 3. Run Optimization Passes (GVN + Constant Folding + Dead Code Elimination)
    dgtl_err_t opt_err = dgtl_optimize_with_profile(stream, DGTL_OPT_O2);
    if (DGTL_IS_ERR(opt_err)) {
        std::cerr << "[ERROR] Optimization failed with code: " << opt_err.code << "\n";
        dgtl_func_builder_destroy(fb);
        dgtl_stream_destroy(stream);
        dgtl_context_destroy(ctx);
        return 1;
    }

    const size_t opt_nodes = dgtl_stream_size(stream);
    std::cout << "[Step 2] Optimization complete. Node Count: " << opt_nodes << "\n";

    // 4. JIT Strike Lowering
    dgtl_strike_result_t strike = dgtl_strike_stream(stream, DGTL_TARGET_X86_64);
    if (!strike.ok || strike.code_size == 0) {
        std::cerr << "[ERROR] Target strike lowering failed\n";
        dgtl_func_builder_destroy(fb);
        dgtl_stream_destroy(stream);
        dgtl_context_destroy(ctx);
        return 1;
    }

    std::cout << "[Step 3] Machine lowering complete. Emitted bytes: " << strike.code_size << "\n";

    dgtl_func_builder_destroy(fb);
    dgtl_stream_destroy(stream);
    dgtl_context_destroy(ctx);

    std::cout << "--- [PIPELINE TEST PASSED] ---\n";
    return 0;
}
