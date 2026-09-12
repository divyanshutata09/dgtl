/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 * DGTL Test 08 — SDK C API Roundtrip
 *
 * Judges: the public C ABI (dgtl.h) works end-to-end: context creation,
 *         stream construction, IR emission, optimization, hash query,
 *         and safe teardown. Tests all major lifecycle functions.
 */

#include "dgtl_sdk/dgtl.h"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

static void require(bool cond, const char* msg) {
    if (!cond) throw std::runtime_error(std::string("T08: ") + msg);
}

void run_test_t08_sdk_c_api_roundtrip() {
    std::cout << "[T08] SDK C API Roundtrip\n";

    // --- lifecycle ---
    dgtl_context_t* ctx = dgtl_context_create();
    require(ctx != nullptr, "dgtl_context_create() returned null");

    dgtl_stream_t* stream = dgtl_stream_create(ctx, 128);
    require(stream != nullptr, "dgtl_stream_create() returned null");

    // --- emission ---
    const uint32_t c1 = dgtl_emit_const_i64(stream, 100);
    require(c1 != 0, "dgtl_emit_const_i64(100) returned invalid slot");

    const uint32_t c2 = dgtl_emit_const_i64(stream, 200);
    require(c2 != 0, "dgtl_emit_const_i64(200) returned invalid slot");

    const uint32_t sum = dgtl_emit_add_i32(stream, c1, c2);
    require(sum != 0, "dgtl_emit_add_i32() returned invalid slot");

    dgtl_emit_return(stream, sum);

    // --- new C-API structures (Myzon lowering tunnels) ---
    dgtl_emit_label_slot(stream, 10);
    dgtl_emit_label_slot(stream, 20);
    dgtl_emit_constant_str(stream, 12, "test_string", 11);
    dgtl_emit_hardware_affinity(stream, 1);
    dgtl_emit_branch_predictive(stream, c1, 10, 20, 200);
    uint32_t val = dgtl_emit_op_with_result(stream, 15, DGTL_OP_ADD_I64, c1, c2, 0, 0);
    require(val == 15, "dgtl_emit_op_with_result should return slot 15");

    // --- node count ---
    const size_t count = dgtl_stream_node_count(stream);
    require(count >= 3, "dgtl_stream_node_count() should be >= 3 after const+add+return");

    // --- hash before optimization ---
    const uint64_t hash_before = dgtl_stream_logic_hash(stream);
    require(hash_before != 0, "dgtl_stream_logic_hash() before optimize returned 0");

    // --- optimization ---
    const dgtl_status_t opt_status = dgtl_optimize(stream);
    if (!opt_status.ok) {
        throw std::runtime_error(std::string("T08: dgtl_optimize() failed: ") + opt_status.message);
    }

    // --- hash after optimization (must remain non-zero) ---
    const uint64_t hash_after = dgtl_stream_logic_hash(stream);
    require(hash_after != 0, "dgtl_stream_logic_hash() after optimize returned 0");

    // --- version string ---
    const char* ver = dgtl_version_string();
    require(ver != nullptr && ver[0] != '\0', "dgtl_version_string() returned null or empty");

    // --- teardown ---
    dgtl_stream_destroy(stream);
    dgtl_context_destroy(ctx);

    std::cout << "[T08] PASS - full C API lifecycle: create/emit/optimize/hash/destroy OK "
              << "(SDK v" << ver << ")\n";
}
