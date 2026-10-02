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
 * This test targets the current unified public C ABI in dgtl.h.
 * It validates:
 *   1. Public SDK header compilation.
 *   2. Context/stream lifecycle.
 *   3. Low-level C-ABI IR emission.
 *   4. Stream introspection and optimization.
 *   5. Version/ABI metadata.
 *   6. Safe teardown.
 */

#include "dgtl_sdk/dgtl.h"

#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>

static void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

static uint32_t emit_const_i64(dgtl_stream_t* stream, int64_t value) {
    const uint64_t bits = static_cast<uint64_t>(value);
    const uint32_t lo = static_cast<uint32_t>(bits & 0xFFFFFFFFull);
    const uint32_t hi = static_cast<uint32_t>((bits >> 32) & 0xFFFFFFFFull);

    return dgtl_emit_raw(
        stream,
        DGTL_OP_CONST,
        DGTL_FLAG_IS_CONST,
        lo,
        hi,
        DGTL_INVALID_SLOT);
}

int main(void) {
    try {
        std::printf("[SDK smoke] DGTL SDK current C-ABI smoke test\n");

        const dgtl_version_info_t version = dgtl_get_version();

        std::printf(
            "[SDK smoke] version=%u.%u.%u ABI=%u build=%s\n",
            version.major,
            version.minor,
            version.patch,
            version.abi_version,
            version.build_id ? version.build_id : "<unknown>");

        require(version.build_id != nullptr && version.build_id[0] != '\0',
                "dgtl_get_version().build_id is null or empty");
        require(version.abi_version == DGTL_ABI_VERSION,
                "reported ABI version does not match DGTL_ABI_VERSION");

        dgtl_context_t* ctx = dgtl_context_create(0, 64);
        require(ctx != nullptr,
                "dgtl_context_create(0, 64) returned null");

        dgtl_stream_t* stream = dgtl_stream_create(ctx, 128);
        require(stream != nullptr,
                "dgtl_stream_create() returned null");

        const uint32_t c1 = emit_const_i64(stream, 100);
        require(c1 != DGTL_INVALID_SLOT,
                "CONST(100) emission failed");

        const uint32_t c2 = emit_const_i64(stream, 200);
        require(c2 != DGTL_INVALID_SLOT,
                "CONST(200) emission failed");

        const uint32_t sum = dgtl_emit_raw(
            stream,
            DGTL_OP_ADD_I64,
            DGTL_FLAG_NONE,
            c1,
            c2,
            DGTL_INVALID_SLOT);

        require(sum != DGTL_INVALID_SLOT,
                "ADD_I64 emission failed");

        const uint32_t ret = dgtl_emit_raw(
            stream,
            DGTL_OP_RET,
            DGTL_FLAG_IS_RETURN,
            sum,
            0,
            0);

        require(ret != DGTL_INVALID_SLOT,
                "RET emission failed");

        const std::size_t count = dgtl_stream_size(stream);
        std::printf("[SDK smoke] Stream nodes before optimize: %zu\n", count);
        require(count >= 4,
                "stream should contain at least const+const+add+ret");

        dgtl_node_t node{};
        require(dgtl_stream_get_node(stream, 2, &node),
                "dgtl_stream_get_node() failed for ADD node");
        require(node.opcode == DGTL_OP_ADD_I64,
                "stream node #2 is not ADD_I64");

        const dgtl_err_t opt_status = dgtl_optimize(stream);
        require(!DGTL_IS_ERR(opt_status),
                dgtl_err_message(opt_status));

        const dgtl_perf_metrics_t metrics =
            dgtl_stream_get_perf_metrics(stream);

        require(metrics.total_nodes != 0 || dgtl_stream_size(stream) != 0,
                "stream unexpectedly became empty after optimize");
        require(metrics.logic_hash != 0,
                "optimized stream logic hash is zero");

        std::printf(
            "[SDK smoke] optimized_nodes=%zu logic_hash=0x%016llx\n",
            dgtl_stream_size(stream),
            static_cast<unsigned long long>(metrics.logic_hash));

        dgtl_stream_destroy(stream);
        dgtl_context_destroy(ctx);

        std::printf("[SDK smoke] PASS: current public C ABI lifecycle validated\n");
        return 0;
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "[SDK smoke] FAIL: %s\n", ex.what());
        return 1;
    }
}
