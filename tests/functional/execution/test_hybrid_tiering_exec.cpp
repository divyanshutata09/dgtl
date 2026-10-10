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

void run_test_pipeline_20__tiering() {
    std::cout << "[DGTL] Running Hybrid Tiering Execution Test..." << std::endl;

    dgtl_context_t* ctx = dgtl_context_create(1, 32);
    assert(ctx != nullptr);

    dgtl_stream_t* stream = dgtl_stream_create(ctx, 128);
    assert(stream != nullptr);

    dgtl_func_builder_t* fb = dgtl_func_builder_create(stream, "hybrid_kernel", DGTL_TYPE_I64);
    assert(fb != nullptr);

    dgtl_block_t entry = dgtl_func_append_block(fb, "entry");
    dgtl_func_set_insert_block(fb, entry);

    // Node 0: vr1 = 1337
    dgtl_value_t vr1 = dgtl_build_const_i64(fb, 1337);
    // Node 1: vr2 = vr1 + 1
    dgtl_value_t one = dgtl_build_const_i64(fb, 1);
    dgtl_value_t vr2 = dgtl_build_add(fb, vr1, one);
    // Node 2: ret vr2
    dgtl_build_ret(fb, vr2);

    (void)dgtl_func_builder_finalize(fb);

    dgtl_err_t opt_err = dgtl_optimize_with_profile(stream, DGTL_OPT_O2);
    assert(DGTL_IS_OK(opt_err));

    dgtl_strike_result_t strike = dgtl_strike_stream(stream, DGTL_TARGET_X86_64);
    assert(strike.ok == 1);
    assert(strike.code_size > 0);

    std::cout << "[DGTL] Hybrid tiering strike complete: " << strike.code_size << " bytes emitted." << std::endl;

    dgtl_func_builder_destroy(fb);
    dgtl_stream_destroy(stream);
    dgtl_context_destroy(ctx);

    std::cout << "[DGTL] Hybrid tiering test complete." << std::endl;
}
