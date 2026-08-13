/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 * DGTL Test 13 — SDK Session and Capability Surface
 */

#include "dgtl_sdk/dgtl.h"

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

} // namespace

void run_test_t13_sdk_session_capabilities() {
    const dgtl_capabilities_t caps = dgtl_get_capabilities();
    require(caps.schema_version == DGTL_CAPABILITY_SCHEMA_VERSION, "unexpected capability schema version");
    require(caps.supports_jit != 0u, "public SDK must advertise JIT support");
    require(caps.supports_aot != 0u, "public SDK must advertise AOT support");
    require(caps.supports_hybrid != 0u, "public SDK must advertise hybrid support");
    require(caps.supports_host_symbol_slot_binding != 0u, "public SDK must advertise slot binding support");
    require(dgtl_capabilities_support_target(&caps, DGTL_TARGET_X64) == 1, "public SDK must advertise x64 target support");

    dgtl_context_t* ctx = dgtl_context_create();
    require(ctx != nullptr, "dgtl_context_create returned null");

    dgtl_stream_t* stream = dgtl_stream_create(ctx, 32);
    require(stream != nullptr, "dgtl_stream_create returned null");

    const uint32_t entry_slot = 0x1001u;
    require(dgtl_emit_label(stream, "DGTL_kernel_entry") != 0u, "dgtl_emit_label(DGTL_kernel_entry) failed");
    require(dgtl_emit_function_label_slot(stream, entry_slot) != 0u, "dgtl_emit_function_label_slot failed");

    const uint32_t c1 = dgtl_emit_op_with_result(stream, 0x2001u, DGTL_OP_CONST, 0u, 0u, 11u, DGTL_FLAG_IS_CONST);
    const uint32_t c2 = dgtl_emit_op_with_result(stream, 0x2002u, DGTL_OP_CONST, 0u, 0u, 31u, DGTL_FLAG_IS_CONST);
    require(c1 == 0x2001u, "explicit result slot for const #1 not preserved");
    require(c2 == 0x2002u, "explicit result slot for const #2 not preserved");

    const uint32_t sum = dgtl_emit_op_with_result(stream, 0x2003u, DGTL_OP_ADD, c1, c2, 0u, DGTL_FLAG_NONE);
    require(sum == 0x2003u, "explicit result slot for add not preserved");
    dgtl_emit_return(stream, sum);

    dgtl_session_config_t jit_cfg = dgtl_session_config_default();
    jit_cfg.mode = DGTL_SESSION_MODE_JIT;
    dgtl_session_t* jit = dgtl_session_create(&jit_cfg);
    require(jit != nullptr, "dgtl_session_create(JIT) returned null");

    const dgtl_status_t jit_compile = dgtl_session_compile_stream(jit, stream);
    require(jit_compile.ok != 0, jit_compile.message);

    int64_t run_result = 0;
    const dgtl_status_t run_status = dgtl_session_run_entry_i64(jit, "DGTL_kernel_entry", &run_result);
    require(run_status.ok != 0, run_status.message);
    dgtl_session_destroy(jit);

    dgtl_session_config_t aot_cfg = dgtl_session_config_default();
    aot_cfg.mode = DGTL_SESSION_MODE_AOT;
    dgtl_session_t* aot = dgtl_session_create(&aot_cfg);
    require(aot != nullptr, "dgtl_session_create(AOT) returned null");

    const dgtl_status_t aot_compile = dgtl_session_compile_stream(aot, stream);
    require(aot_compile.ok != 0, aot_compile.message);

    const std::filesystem::path out_path = std::filesystem::temp_directory_path() / "dgtl_t13_session.obj";
    const dgtl_status_t emit_status = dgtl_session_emit_object(aot, out_path.string().c_str());
    require(emit_status.ok != 0, emit_status.message);
    dgtl_session_destroy(aot);

    std::error_code ec;
    std::filesystem::remove(out_path, ec);

    dgtl_stream_destroy(stream);
    dgtl_context_destroy(ctx);
}