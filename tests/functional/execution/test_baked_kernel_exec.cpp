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
#include <iostream>
#include <cassert>
#include <cstdint>

void run_baked_kernel() {
    std::cout << "[DGTL] Running Baked Kernel Execution Test..." << std::endl;

    dgtl_context_t* ctx = dgtl_context_create(1, 32);
    assert(ctx != nullptr);

    dgtl_stream_t* stream = dgtl_stream_create(ctx, 128);
    assert(stream != nullptr);

    dgtl_func_builder_t* fb = dgtl_func_builder_create(stream, "baked_kernel", DGTL_TYPE_I64);
    assert(fb != nullptr);

    dgtl_block_t entry = dgtl_func_append_block(fb, "entry");
    dgtl_func_set_insert_block(fb, entry);

    // Build: r1 = 42; r2 = r1 + r1; return r2 (expect 84)
    dgtl_value_t r1 = dgtl_build_const_i64(fb, 42);
    dgtl_value_t r2 = dgtl_build_add(fb, r1, r1);
    dgtl_build_ret(fb, r2);

    (void)dgtl_func_builder_finalize(fb);

    dgtl_err_t opt_err = dgtl_optimize_with_profile(stream, DGTL_OPT_O2);
    assert(DGTL_IS_OK(opt_err));

    dgtl_strike_result_t strike = dgtl_strike_stream(stream, DGTL_TARGET_X86_64);
    assert(strike.ok == 1);
    assert(strike.code_size > 0);

    std::cout << "[DGTL] Baked kernel successfully compiled: " << strike.code_size << " bytes emitted." << std::endl;

    dgtl_func_builder_destroy(fb);
    dgtl_stream_destroy(stream);
    dgtl_context_destroy(ctx);

    std::cout << "[DGTL] Baked kernel test complete." << std::endl;
}
