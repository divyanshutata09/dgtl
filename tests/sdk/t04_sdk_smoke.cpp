/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 */

#include "dgtl_sdk/dgtl.h"

#include <iostream>
#include <stdexcept>

void run_test_t04_sdk_smoke() {
    std::cout << "[T04] SDK smoke\n";

    dgtl_context_t* ctx = dgtl_context_create();
    if (ctx == nullptr) {
        throw std::runtime_error("T04: dgtl_context_create failed");
    }

    dgtl_stream_t* s = dgtl_stream_create(ctx, 32);
    if (s == nullptr) {
        dgtl_context_destroy(ctx);
        throw std::runtime_error("T04: dgtl_stream_create failed");
    }

    uint32_t a = dgtl_emit_const_i64(s, 100);
    uint32_t b = dgtl_emit_const_i64(s, 23);
    uint32_t c = dgtl_emit_add_i32(s, a, b);
    dgtl_emit_return(s, c);

    const dgtl_status_t st = dgtl_optimize(s);
    if (!st.ok) {
        dgtl_stream_destroy(s);
        dgtl_context_destroy(ctx);
        throw std::runtime_error("T04: dgtl_optimize failed");
    }

    if (dgtl_stream_node_count(s) == 0 || dgtl_stream_logic_hash(s) == 0u) {
        dgtl_stream_destroy(s);
        dgtl_context_destroy(ctx);
        throw std::runtime_error("T04: invalid stream state after optimize");
    }

    dgtl_stream_destroy(s);
    dgtl_context_destroy(ctx);

    std::cout << "[T04] PASS - SDK smoke OK\n";
}
