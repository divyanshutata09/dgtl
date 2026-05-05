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

#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

} // namespace

void run_test_sdk_strike_code_size() {
    dgtl_context_t* ctx = dgtl_context_create();
    require(ctx != nullptr, "sdk context create returned null");

    dgtl_stream_t* stream = dgtl_stream_create(ctx, 64);
    require(stream != nullptr, "sdk stream create returned null");

    const uint32_t c1 = dgtl_emit_const_i64(stream, 100u);
    const uint32_t c2 = dgtl_emit_const_i64(stream, 200u);
    const uint32_t add = dgtl_emit_add_i32(stream, c1, c2);
    dgtl_emit_return(stream, add);

    const uint64_t pre_hash = dgtl_stream_logic_hash(stream);

    const dgtl_target_t targets[] = {
        DGTL_TARGET_X64,
        DGTL_TARGET_NASM,
        DGTL_TARGET_WASM,
        DGTL_TARGET_ARM64
    };

    bool saw_success = false;
    for (dgtl_target_t target : targets) {
        const dgtl_strike_result_t strike = dgtl_strike_stream(stream, target);
        if (strike.ok == 1) {
            require(strike.code_size > 0u, "sdk strike code_size is not byte-sized output");
            require(strike.logic_hash == pre_hash, "sdk strike logic hash mismatch");
            saw_success = true;
            break;
        }

        require(strike.code_size == 0u, "sdk strike failure path should report zero code_size");
    }

    require(saw_success, "sdk strike failed on all tested targets");

    dgtl_stream_destroy(stream);
    dgtl_context_destroy(ctx);
}
