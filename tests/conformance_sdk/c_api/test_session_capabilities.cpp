/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 * DGTL Test 13 — SDK Session and Capability Surface
 *
 * Judges: the current authoritative public C ABI exposes hardware
 *         capabilities, context/stream lifecycle, JIT/AOT session creation,
 *         compilation, execution, and object emission.
 */

#include "dgtl_sdk/dgtl.h"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace {

void require(bool cond, const char* msg) {
    if (!cond) {
        throw std::runtime_error(std::string("T13: ") + msg);
    }
}

uint32_t emit_const_i64(dgtl_stream_t* stream, int64_t value) {
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

void require_ok(dgtl_err_t err, const char* operation) {
    if (DGTL_IS_ERR(err)) {
        throw std::runtime_error(
            std::string("T13: ") + operation + " failed: " +
            dgtl_err_message(err));
    }
}

} // namespace

void run_test_t13_sdk_session_capabilities() {
    // --- public hardware capability surface ---
    const dgtl_capabilities_t caps = dgtl_query_capabilities();

    require(caps.core_count > 0, "public SDK reported zero CPU cores");
    require(caps.numa_node_count > 0, "public SDK reported zero NUMA nodes");

    // --- SDK lifecycle ---
    dgtl_context_t* ctx = dgtl_context_create(0, 64);
    require(ctx != nullptr, "dgtl_context_create(0, 64) returned null");

    dgtl_stream_t* stream = dgtl_stream_create(ctx, 32);
    require(stream != nullptr, "dgtl_stream_create() returned null");

    // Build a tiny executable stream: 11 + 31 -> return.
    const uint32_t c1 = emit_const_i64(stream, 11);
    const uint32_t c2 = emit_const_i64(stream, 31);
    require(c1 != DGTL_INVALID_SLOT && c2 != DGTL_INVALID_SLOT,
            "constant emission failed");

    const uint32_t sum = dgtl_emit_raw(
        stream,
        DGTL_OP_ADD_I64,
        DGTL_FLAG_NONE,
        c1,
        c2,
        DGTL_INVALID_SLOT);
    require(sum != DGTL_INVALID_SLOT, "ADD_I64 emission failed");

    const uint32_t ret = dgtl_emit_raw(
        stream,
        DGTL_OP_RET,
        DGTL_FLAG_IS_RETURN,
        sum,
        0,
        0);
    require(ret != DGTL_INVALID_SLOT, "RET emission failed");

    // --- JIT session ---
    dgtl_session_config_t jit_cfg = dgtl_session_config_default();
    jit_cfg.mode = DGTL_SESSION_MODE_JIT;

    dgtl_session_t* jit = dgtl_session_create(&jit_cfg);
    require(jit != nullptr, "dgtl_session_create(JIT) returned null");

    require_ok(
        dgtl_session_compile(jit, stream),
        "dgtl_session_compile(JIT)");

    int64_t run_result = 0;
    require_ok(
        dgtl_session_run_i64(jit, "main", nullptr, 0, &run_result),
        "dgtl_session_run_i64(JIT)");

    require(run_result == 42,
            "JIT execution returned unexpected result; expected 42");

    dgtl_session_destroy(jit);

    // --- AOT session/object emission ---
    dgtl_session_config_t aot_cfg = dgtl_session_config_default();
    aot_cfg.mode = DGTL_SESSION_MODE_AOT;

    dgtl_session_t* aot = dgtl_session_create(&aot_cfg);
    require(aot != nullptr, "dgtl_session_create(AOT) returned null");

    require_ok(
        dgtl_session_compile(aot, stream),
        "dgtl_session_compile(AOT)");

    const std::filesystem::path out_path =
        std::filesystem::temp_directory_path() / "dgtl_t13_session.obj";

    require_ok(
        dgtl_session_emit_object(aot, out_path.string().c_str()),
        "dgtl_session_emit_object(AOT)");

    dgtl_session_destroy(aot);

    std::error_code ec;
    std::filesystem::remove(out_path, ec);

    // --- ABI/version surface ---
    const dgtl_version_info_t version = dgtl_get_version();
    require(version.abi_version == DGTL_ABI_VERSION,
            "reported ABI version mismatch");
    require(dgtl_abi_compatible(version.abi_version),
            "current ABI version is not self-compatible");

    dgtl_stream_destroy(stream);
    dgtl_context_destroy(ctx);
}
