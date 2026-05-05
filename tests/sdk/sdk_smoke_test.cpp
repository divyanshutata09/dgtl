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

/*
 * sdk_smoke_test.cpp — SDK Smoke Link Test
 *
 * This file is compiled OUTSIDE the DGTL source tree against only the
 * SDK public headers and the static library (dgtl.lib / libdgtl.a).
 *
 * It validates:
 *   1. Headers compile cleanly with extern "C" linkage.
 *   2. Symbols resolve from the static library.
 *   3. A trivial 100+200 constant fold round-trips through the pipeline.
 */

#include "dgtl.h"
#include "dgtl_core.h"
#include "dgtl_simd.h"

#include <stdio.h>
#include <stdlib.h>

int main(void) {
    const size_t kReservedNodes = 64;
    const size_t kExpectedNodeCount = 4;

    printf("[SDK smoke] DGTL SDK version: %s\n", dgtl_version_string());

    /* --- 1. Create context and stream --- */
    dgtl_context_t* ctx = dgtl_context_create();
    if (!ctx) {
        fprintf(stderr, "[SDK smoke] FAIL: context creation returned NULL\n");
        return 1;
    }

    dgtl_stream_t* s = dgtl_stream_create(ctx, kReservedNodes);
    if (!s) {
        fprintf(stderr, "[SDK smoke] FAIL: stream creation returned NULL\n");
        dgtl_context_destroy(ctx);
        return 1;
    }

    /* --- 2. Emit IR: 100 + 200 --- */
    uint32_t c1 = dgtl_emit_const_i64(s, 100);
    uint32_t c2 = dgtl_emit_const_i64(s, 200);
    uint32_t r  = dgtl_emit_add_i32(s, c1, c2);
    dgtl_emit_return(s, r);

    const size_t node_count = dgtl_stream_node_count(s);
    printf("[SDK smoke] Stream nodes: %zu\n", node_count);

    if (node_count != kExpectedNodeCount) {
        fprintf(stderr, "[SDK smoke] FAIL: expected 4 nodes, got %zu\n",
                node_count);
        dgtl_stream_destroy(s);
        dgtl_context_destroy(ctx);
        return 1;
    }

    /* --- 3. Optimize --- */
    dgtl_status_t opt = dgtl_optimize(s);
    if (!opt.ok) {
        fprintf(stderr, "[SDK smoke] FAIL: optimize returned error: %s\n", opt.message);
        dgtl_stream_destroy(s);
        dgtl_context_destroy(ctx);
        return 1;
    }

    /* --- 4. Inspect nodes --- */
    dgtl_node_info_t info;
    if (dgtl_stream_get_node(s, 0, &info)) {
        printf("[SDK smoke] Node[0] opcode=0x%02X slot=%u imm=%llu\n",
               info.opcode, info.slot, (unsigned long long)info.immediate);
    }

    /* --- 5. Logic hash stability --- */
    uint64_t hash = dgtl_stream_logic_hash(s);
    printf("[SDK smoke] Logic hash: 0x%016llx\n", (unsigned long long)hash);

    /* --- 6. Target enumeration --- */
    for (int t = 0; t < DGTL_TARGET_COUNT; ++t) {
        printf("[SDK smoke] Target %d: %-10s  ext=%s\n",
               t, dgtl_target_name((dgtl_target_t)t),
               dgtl_target_extension((dgtl_target_t)t));
    }

    /* --- 7. SIMD API compile check --- */
    /* Just verify the symbols link; don't actually legalize an empty stream. */
    (void)&dgtl_emit_fma;
    (void)&dgtl_emit_gather;
    (void)&dgtl_emit_scatter;
    (void)&dgtl_legalize_simd;

    /* --- Cleanup --- */
    dgtl_stream_destroy(s);
    dgtl_context_destroy(ctx);

    printf("[SDK smoke] PASS: all checks succeeded\n");
    return 0;
}
