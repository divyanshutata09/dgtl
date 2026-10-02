/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 * DGTL Test 08 — SDK C API Roundtrip
 *
 * Judges: the authoritative public C ABI (dgtl.h) works end-to-end:
 *         context creation, stream construction, raw IR emission,
 *         optimization, integrity/performance-metric query, version query,
 *         and safe teardown.
 */

#include "dgtl_sdk/dgtl.h"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

static void require(bool cond, const char* msg) {
    if (!cond) throw std::runtime_error(std::string("T08: ") + msg);
}

static uint32_t emit_const_i64(dgtl_stream_t* stream, int64_t value) {
    const uint32_t lo = static_cast<uint32_t>(
        static_cast<uint64_t>(value) & 0xFFFFFFFFull);
    const uint32_t hi = static_cast<uint32_t>(
        (static_cast<uint64_t>(value) >> 32) & 0xFFFFFFFFull);

    return dgtl_emit_raw(
        stream,
        DGTL_OP_CONST,
        DGTL_FLAG_IS_CONST,
        lo,
        hi,
        DGTL_INVALID_SLOT);
}

void run_test_t08_sdk_c_api_roundtrip() {
    std::cout << "[T08] SDK C API Roundtrip\n";

    // --- lifecycle ---
    dgtl_context_t* ctx = dgtl_context_create(0, 64);
    require(ctx != nullptr, "dgtl_context_create(0, 64) returned null");

    dgtl_stream_t* stream = dgtl_stream_create(ctx, 128);
    require(stream != nullptr, "dgtl_stream_create() returned null");

    // --- public low-level C-ABI emission ---
    const uint32_t c1 = emit_const_i64(stream, 100);
    require(c1 != DGTL_INVALID_SLOT, "CONST(100) returned invalid slot");

    const uint32_t c2 = emit_const_i64(stream, 200);
    require(c2 != DGTL_INVALID_SLOT, "CONST(200) returned invalid slot");

    const uint32_t sum = dgtl_emit_raw(
        stream,
        DGTL_OP_ADD_I64,
        DGTL_FLAG_NONE,
        c1,
        c2,
        DGTL_INVALID_SLOT);
    require(sum != DGTL_INVALID_SLOT, "ADD_I64 returned invalid slot");

    const uint32_t ret = dgtl_emit_raw(
        stream,
        DGTL_OP_RET,
        DGTL_FLAG_IS_RETURN,
        sum,
        0,
        0);
    require(ret != DGTL_INVALID_SLOT, "RET emission failed");

    // --- stream introspection ---
    const size_t count = dgtl_stream_size(stream);
    require(count >= 4, "stream size should be >= 4 after const+const+add+ret");

    dgtl_node_t node{};
    require(dgtl_stream_get_node(stream, 2, &node),
            "dgtl_stream_get_node() failed for ADD node");
    require(node.opcode == DGTL_OP_ADD_I64,
            "stream node #2 is not ADD_I64");

    // --- optimization ---
    const dgtl_err_t opt_status = dgtl_optimize(stream);
    if (DGTL_IS_ERR(opt_status)) {
        throw std::runtime_error(
            std::string("T08: dgtl_optimize() failed: ") +
            dgtl_err_message(opt_status));
    }

    // --- integrity/performance metric query ---
    const dgtl_perf_metrics_t metrics = dgtl_stream_get_perf_metrics(stream);
    require(metrics.total_nodes != 0 || dgtl_stream_size(stream) != 0,
            "performance metrics/stream became empty unexpectedly");
    require(metrics.logic_hash != 0,
            "dgtl_stream_get_perf_metrics().logic_hash returned 0");

    // --- version ---
    const dgtl_version_info_t version = dgtl_get_version();
    require(version.build_id != nullptr && version.build_id[0] != '\0',
            "dgtl_get_version().build_id is null or empty");
    require(version.abi_version == DGTL_ABI_VERSION,
            "reported ABI version does not match DGTL_ABI_VERSION");

    // --- teardown ---
    dgtl_stream_destroy(stream);
    dgtl_context_destroy(ctx);

    std::cout << "[T08] PASS - full C API lifecycle: create/emit/optimize/query/destroy OK "
              << "(ABI " << version.abi_version << ", build " << version.build_id << ")\n";
}
