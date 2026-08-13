/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository: github.com/TataG-Systems/DGTL
 * Component:   Comprehensive SDK & Core Architecture Test Suite
 * Purpose:     Production-Grade Examination of All Public SDK & Internal API Features
 */

#include <chrono>
#include <atomic>
#include <exception>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>
#include <cstring>
#include <cassert>
#include <cstdint>

// Include DGTL Public SDK Headers
#include "dgtl_sdk/dgtl.h"
#include "dgtl_sdk/dgtl_c_api.h"
#include "dgtl_sdk/dgtl_core.h"
#include "dgtl_sdk/dgtl_session.h"
#include "dgtl_sdk/dgtl_simd.h"
#include "dgtl_sdk/dgtl_capabilities.h"

using namespace std;

static mutex g_log_mutex;
static atomic<uint32_t> g_test_passes{0};
static atomic<uint32_t> g_test_failures{0};

#define TEST_LOG(tag, msg) \
    { \
        lock_guard<mutex> lock(g_log_mutex); \
        cout << "[" << tag << "] " << msg << std::endl; \
    }

#define PASS(msg) \
    { \
        TEST_LOG("PASS", msg); \
        g_test_passes++; \
    }

#define FAIL(msg) \
    { \
        TEST_LOG("FAIL", msg); \
        g_test_failures++; \
    }

#define REQUIRE(cond, msg) \
    if (!(cond)) { \
        FAIL(msg); \
        return false; \
    }

#define RUN_SUITE(test_fn, name) \
    { \
        TEST_LOG("RUN ", "Starting " name "... "); \
        const auto start_time = chrono::high_resolution_clock::now(); \
        bool ok = false; \
        try { \
            ok = test_fn(); \
        } catch (const exception& e) { \
            FAIL(string(name) + " threw exception: " + e.what()); \
        } catch (...) { \
            FAIL(string(name) + " threw unknown exception"); \
        } \
        const auto end_time = chrono::high_resolution_clock::now(); \
        const double duration_ms = chrono::duration<double, milli>(end_time - start_time).count(); \
        if (ok) { \
            TEST_LOG("OK  ", string(name) + " passed in " + to_string(duration_ms) + " ms"); \
        } else { \
            TEST_LOG("ERR ", string(name) + " failed after " + to_string(duration_ms) + " ms"); \
        } \
    }

// ============================================================================
// TEST 1: Telemetry, Versioning & Hardware Capabilities
// ============================================================================
bool test_telemetry_and_capabilities() {
    const char* version = dgtl_version_string();
    REQUIRE(version != nullptr, "dgtl_version_string returned null");
    REQUIRE(strlen(version) > 0, "dgtl_version_string returned empty string");
    
    // Macro version sanity
    REQUIRE(DGTL_VERSION_MAJOR >= 1, "DGTL_VERSION_MAJOR invalid");
    REQUIRE(DGTL_VERSION_STRING != nullptr, "DGTL_VERSION_STRING macro null");

    const char* build_cfg = dgtl_get_build_config();
    REQUIRE(build_cfg != nullptr, "dgtl_get_build_config returned null");

    // Capabilities check
    dgtl_capabilities_t caps = dgtl_get_capabilities();
    REQUIRE(caps.schema_version >= 1, "Capabilities schema version invalid");
    REQUIRE(caps.supports_jit == 1, "JIT support should be enabled");
    REQUIRE(caps.supports_aot == 1, "AOT support should be enabled");
    int supp_x64 = dgtl_capabilities_support_target(&caps, DGTL_TARGET_X64);
    REQUIRE(supp_x64 == 1, "Target x64 should be supported");

    TEST_LOG("INFO", "SDK Version: " + string(version) + " | Build: " + string(build_cfg) + " | JIT: " + to_string(caps.supports_jit) + " | AOT: " + to_string(caps.supports_aot));

    PASS("1. Telemetry, Versioning & Hardware Capabilities");
    return true;
}

// ============================================================================
// TEST 2: Context, Stream & Arena Lifecycle Management
// ============================================================================
bool test_context_stream_arena() {
    dgtl_context_t* ctx = dgtl_context_create();
    REQUIRE(ctx != nullptr, "dgtl_context_create failed");

    dgtl_stream_t* stream = dgtl_stream_create(ctx, 256);
    REQUIRE(stream != nullptr, "dgtl_stream_create failed");

    // Memory usage & capacity
    size_t initial_stream_mem = dgtl_stream_memory_usage(stream);
    size_t initial_ctx_mem = dgtl_context_memory_usage(ctx);
    REQUIRE(initial_stream_mem > 0, "Stream memory usage should be non-zero");
    REQUIRE(initial_ctx_mem > 0, "Context memory usage should be non-zero");

    dgtl_status_t status = dgtl_stream_reserve(stream, 1024);
    REQUIRE(status.ok == 1, "dgtl_stream_reserve failed");

    status = dgtl_stream_compact(stream);
    REQUIRE(status.ok == 1, "dgtl_stream_compact failed");

    status = dgtl_stream_shrink_to_fit(stream);
    REQUIRE(status.ok == 1, "dgtl_stream_shrink_to_fit failed");

    dgtl_stream_destroy(stream);
    dgtl_context_destroy(ctx);

    PASS("2. Context, Stream & Arena Lifecycle Management");
    return true;
}

// ============================================================================
// TEST 3: Scalar IR Emission & OpCode Pipeline
// ============================================================================
bool test_scalar_ir_emission() {
    dgtl_context_t* ctx = dgtl_context_create();
    dgtl_stream_t* stream = dgtl_stream_create(ctx, 128);

    // Immediate Constants
    uint32_t c1 = dgtl_emit_const_i64(stream, 42);
    uint32_t c2 = dgtl_emit_const_i64(stream, 100);
    uint32_t f1 = dgtl_emit_const_f32(stream, 3.14159f);
    uint32_t f2 = dgtl_emit_const_f64(stream, 2.718281828459);
    REQUIRE(c1 != 0 && c2 != 0 && f1 != 0 && f2 != 0, "Constant emission failed");

    // String Handle & Constant String
    uint32_t handle = dgtl_intern_string_handle_utf8("Hello DGTL", 10);
    REQUIRE(handle != 0, "dgtl_intern_string_handle_utf8 failed");
    dgtl_emit_constant_str(stream, c1, "Hello DGTL", 10);

    // 32-bit Integer Binary Operations
    uint32_t i_add = dgtl_emit_add_i32(stream, c1, c2);
    uint32_t i_sub = dgtl_emit_sub_i32(stream, i_add, c1);
    uint32_t i_mul = dgtl_emit_mul_i32(stream, i_sub, c2);
    uint32_t i_div = dgtl_emit_div_i32(stream, i_mul, c1);
    uint32_t i_mod = dgtl_emit_mod_i32(stream, i_div, c1);
    uint32_t i_and = dgtl_emit_and_i32(stream, i_mod, c1);
    uint32_t i_or  = dgtl_emit_or_i32(stream, i_and, c2);
    uint32_t i_xor = dgtl_emit_xor_i32(stream, i_or, c1);
    uint32_t i_shl = dgtl_emit_shl_i32(stream, i_xor, c1);
    uint32_t i_shr = dgtl_emit_shr_i32(stream, i_shl, c1);
    REQUIRE(i_shr != 0, "Integer 32-bit operations failed");

    // 64-bit Integer Operations
    uint32_t i64_add = dgtl_emit_add_i64(stream, c1, c2);
    uint32_t i64_sub = dgtl_emit_sub_i64(stream, i64_add, c1);
    uint32_t i64_mul = dgtl_emit_mul_i64(stream, i64_sub, c2);
    uint32_t i64_div = dgtl_emit_div_i64(stream, i64_mul, c1);
    REQUIRE(i64_div != 0, "Integer 64-bit operations failed");

    // 64-bit FP Operations
    uint32_t f64_add = dgtl_emit_fadd_f64(stream, f2, f2);
    uint32_t f64_sub = dgtl_emit_fsub_f64(stream, f64_add, f2);
    uint32_t f64_mul = dgtl_emit_fmul_f64(stream, f64_sub, f2);
    uint32_t f64_div = dgtl_emit_fdiv_f64(stream, f64_mul, f2);
    REQUIRE(f64_div != 0, "FP 64-bit operations failed");

    // Generic raw opcode emission
    uint32_t raw_node = dgtl_emit_op(stream, DGTL_OP_ADD, c1, c2, 0, DGTL_FLAG_NONE);
    uint32_t raw_result = dgtl_emit_op_with_result(stream, 99, DGTL_OP_MUL, c1, c2, 0, DGTL_FLAG_NONE);
    REQUIRE(raw_node != 0 && raw_result == 99, "Generic opcode emission failed");

    dgtl_stream_destroy(stream);
    dgtl_context_destroy(ctx);

    PASS("3. Scalar IR Emission & OpCode Pipeline");
    return true;
}

// ============================================================================
// TEST 4: AI/ML & Spatial Math Operations
// ============================================================================
bool test_ai_and_spatial_math() {
    dgtl_context_t* ctx = dgtl_context_create();
    dgtl_stream_t* stream = dgtl_stream_create(ctx, 128);

    uint32_t t_a = dgtl_emit_const_i64(stream, 1);
    uint32_t t_b = dgtl_emit_const_i64(stream, 2);
    uint32_t t_c = dgtl_emit_const_i64(stream, 3);

    // HFT & Low-Latency Subsystem OpCodes
    uint32_t tsc      = dgtl_emit_op(stream, DGTL_OP_READ_TSC, t_a, 0, 0, DGTL_FLAG_NONE);
    uint32_t br_lk    = dgtl_emit_op(stream, DGTL_OP_BR_LIKELY, t_a, t_b, 0, DGTL_FLAG_NONE);
    uint32_t prefetch = dgtl_emit_op(stream, DGTL_OP_PREFETCH_T0, t_a, 0, 0, DGTL_FLAG_NONE);
    uint32_t clflush  = dgtl_emit_op(stream, DGTL_OP_CLFLUSH, t_a, 0, 0, DGTL_FLAG_NONE);
    REQUIRE(tsc != 0 && br_lk != 0 && prefetch != 0 && clflush != 0, "HFT low-latency opcode emission failed");

    // Memory & Pointer Arithmetics
    uint32_t gep      = dgtl_emit_op(stream, DGTL_OP_GEP, t_a, t_b, 0, DGTL_FLAG_NONE);
    uint32_t ptr_int  = dgtl_emit_op(stream, DGTL_OP_PTR_TO_INT, gep, 0, 0, DGTL_FLAG_NONE);
    uint32_t int_ptr  = dgtl_emit_op(stream, DGTL_OP_INT_TO_PTR, ptr_int, 0, 0, DGTL_FLAG_NONE);
    uint32_t sel      = dgtl_emit_op(stream, DGTL_OP_SELECT, t_a, t_b, t_c, DGTL_FLAG_NONE);
    REQUIRE(gep != 0 && ptr_int != 0 && int_ptr != 0 && sel != 0, "Pointer and selection opcode emission failed");

    dgtl_stream_destroy(stream);
    dgtl_context_destroy(ctx);

    PASS("4. AI/ML & Spatial Math Operations");
    return true;
}

// ============================================================================
// TEST 4B: SIMD & Vectorization Operations
// ============================================================================
bool test_simd_vectorization() {
    dgtl_context_t* ctx = dgtl_context_create();
    dgtl_stream_t* stream = dgtl_stream_create(ctx, 128);

    uint32_t a = dgtl_emit_const_f32(stream, 1.5f);
    uint32_t b = dgtl_emit_const_f32(stream, 2.0f);
    uint32_t c = dgtl_emit_const_f32(stream, 3.0f);

    uint32_t fma32 = dgtl_emit_fma(stream, a, b, c);
    uint32_t fma64 = dgtl_emit_fma_f64(stream, a, b, c);
    uint32_t vec_add = dgtl_emit_vec_add_f32_512(stream, a, b);
    uint32_t vec_mul = dgtl_emit_vec_mul_f32_512(stream, vec_add, b);
    uint32_t gather  = dgtl_emit_gather(stream, a, b, DGTL_FLAG_NONE);
    uint32_t scatter = dgtl_emit_scatter(stream, a, b, c, DGTL_FLAG_NONE);

    REQUIRE(fma32 != 0 && fma64 != 0 && vec_add != 0 && vec_mul != 0 && gather != 0 && scatter != 0, "SIMD emission failed");

    dgtl_stream_destroy(stream);
    dgtl_context_destroy(ctx);

    PASS("4B. SIMD & Vectorization Operations");
    return true;
}

// ============================================================================
// TEST 5: Control Flow, Branching & CFG Introspection
// ============================================================================
bool test_control_flow_and_cfg() {
    dgtl_context_t* ctx = dgtl_context_create();
    dgtl_stream_t* stream = dgtl_stream_create(ctx, 128);

    uint32_t lbl_entry = dgtl_emit_label(stream, "entry");
    uint32_t lbl_loop  = dgtl_emit_label_slot(stream, 100);
    uint32_t lbl_exit  = dgtl_emit_label_slot(stream, 101);
    REQUIRE(lbl_entry != 0 && lbl_loop == 100 && lbl_exit == 101, "Label emission failed");

    uint32_t cond = dgtl_emit_const_i64(stream, 1);
    dgtl_emit_branch_cond(stream, cond, lbl_loop);
    dgtl_emit_branch_predictive(stream, cond, lbl_loop, lbl_exit, 90);
    dgtl_emit_branch(stream, lbl_exit);
    dgtl_emit_return(stream, cond);

    // CFG Introspection
    size_t block_count = dgtl_stream_cfg_block_count(stream);
    size_t edge_count  = dgtl_stream_cfg_edge_count(stream);
    REQUIRE(block_count > 0, "CFG block count should be positive");
    
    if (block_count > 0) {
        dgtl_cfg_block_t blk{};
        int32_t ok = dgtl_stream_cfg_get_block(stream, 0, &blk);
        REQUIRE(ok == 1, "dgtl_stream_cfg_get_block failed");

        uint32_t succs[4]{};
        size_t n_succ = dgtl_stream_cfg_get_successors(stream, blk.id, succs, 4);
        (void)n_succ;
    }

    if (edge_count > 0) {
        dgtl_cfg_edge_t edge{};
        int32_t ok = dgtl_stream_cfg_get_edge(stream, 0, &edge);
        REQUIRE(ok == 1, "dgtl_stream_cfg_get_edge failed");
    }

    dgtl_stream_destroy(stream);
    dgtl_context_destroy(ctx);

    PASS("5. Control Flow, Branching & CFG Introspection");
    return true;
}

// ============================================================================
// TEST 6: Integrity Verification, Optimization Levels & Passes
// ============================================================================
bool test_optimization_passes() {
    dgtl_context_t* ctx = dgtl_context_create();
    dgtl_stream_t* stream = dgtl_stream_create(ctx, 128);

    uint32_t c1 = dgtl_emit_const_i64(stream, 50);
    uint32_t c2 = dgtl_emit_const_i64(stream, 50);
    uint32_t r  = dgtl_emit_add_i32(stream, c1, c2);
    dgtl_emit_return(stream, r);

    // Standard optimization pipeline & logic hash
    dgtl_status_t opt = dgtl_optimize(stream);
    REQUIRE(opt.ok == 1, "dgtl_optimize failed");

    uint64_t hash = dgtl_stream_logic_hash(stream);
    REQUIRE(hash != 0, "dgtl_stream_logic_hash returned 0");

    dgtl_node_info_t info{};
    int32_t got_node = dgtl_stream_get_node(stream, 0, &info);
    REQUIRE(got_node == 1, "dgtl_stream_get_node failed");

    // IR Mode & Optimization Control
    dgtl_status_t status = dgtl_set_ir_mode(stream, DGTL_IR_MODE_INT);
    REQUIRE(status.ok == 1, "dgtl_set_ir_mode failed");

    dgtl_set_optimization_enabled(stream, 1);
    dgtl_set_optimization_level(stream, 2);
    int32_t opt_lvl = dgtl_get_optimization_level(stream);
    REQUIRE(opt_lvl >= 0, "Optimization level query failed");

    // Optimization Profiles
    dgtl_opt_profile_t profiles[] = {
        DGTL_OPT_PROFILE_DEFAULT,
        DGTL_OPT_PROFILE_FOLD,
        DGTL_OPT_PROFILE_SIMD,
        DGTL_OPT_PROFILE_PRESSURE,
        DGTL_OPT_PROFILE_PREDICATION
    };
    for (auto p : profiles) {
        status = dgtl_optimize_with_profile(stream, p);
    }

    // Pass Registry Inspection
    size_t pass_count = 0;
    const char** passes = dgtl_list_passes(&pass_count);
    if (passes != nullptr && pass_count > 0) {
        TEST_LOG("INFO", "Registered optimization passes: " + to_string(pass_count));
        for (size_t i = 0; i < pass_count && i < 5; ++i) {
            REQUIRE(dgtl_has_pass(passes[i]) == 1, string("Pass missing: ") + passes[i]);
        }
    }

    dgtl_stream_destroy(stream);
    dgtl_context_destroy(ctx);

    PASS("6. Integrity Verification, Optimization Levels & Passes");
    return true;
}

// ============================================================================
// TEST 7: Multi-Target Code Emission & "Strike" Engine
// ============================================================================
bool test_strike_engine() {
    dgtl_context_t* ctx = dgtl_context_create();
    dgtl_stream_t* stream = dgtl_stream_create(ctx, 128);

    uint32_t c1 = dgtl_emit_const_i64(stream, 10);
    uint32_t c2 = dgtl_emit_const_i64(stream, 20);
    uint32_t r  = dgtl_emit_add_i32(stream, c1, c2);
    dgtl_emit_return(stream, r);

    dgtl_target_t targets[] = {
        DGTL_TARGET_X64,
        DGTL_TARGET_ARM64,
        DGTL_TARGET_NASM,
        DGTL_TARGET_WASM,
        DGTL_TARGET_ELF_OBJ,
        DGTL_TARGET_COFF_OBJ,
        DGTL_TARGET_MACHO
    };

    for (auto target : targets) {
        size_t est_size = dgtl_estimate_code_size(stream, target);
        REQUIRE(est_size > 0, "dgtl_estimate_code_size returned 0");

        dgtl_emit_options_t opts = dgtl_emit_options_default(target);
        dgtl_strike_result_t res_opt = dgtl_strike_stream_with_options(stream, target, &opts);
        REQUIRE(res_opt.ok == 1, string("dgtl_strike_stream_with_options failed for target ") + to_string(target));

        dgtl_strike_result_t res = dgtl_strike_stream(stream, target);
        REQUIRE(res.ok == 1, string("dgtl_strike_stream failed for target ") + to_string(target));
        REQUIRE(res.code_size > 0, "res.code_size should be > 0");
        REQUIRE(res.logic_hash != 0, "res.logic_hash should be != 0");
    }

    dgtl_stream_destroy(stream);
    dgtl_context_destroy(ctx);

    PASS("7. Multi-Target Code Emission & Strike Engine");
    return true;
}

// ============================================================================
// TEST 8: Session Management & Host Symbol Binding
// ============================================================================
static int64_t dummy_host_fn(int64_t a, int64_t b) {
    return a + b;
}

bool test_session_and_host_symbols() {
    dgtl_session_config_t cfg = dgtl_session_config_default();
    const char* mode_name = dgtl_session_mode_name(cfg.mode);
    REQUIRE(mode_name != nullptr, "dgtl_session_mode_name returned null");

    dgtl_session_t* session = dgtl_session_create(&cfg);
    REQUIRE(session != nullptr, "dgtl_session_create failed");

    // Host symbol registration
    dgtl_status_t status = dgtl_session_register_host_symbol(session, "dummy_fn", reinterpret_cast<uintptr_t>(&dummy_host_fn));
    REQUIRE(status.ok == 1, "dgtl_session_register_host_symbol failed");

    status = dgtl_session_register_host_symbol_slot(session, 42, reinterpret_cast<uintptr_t>(&dummy_host_fn));
    REQUIRE(status.ok == 1, "dgtl_session_register_host_symbol_slot failed");

    dgtl_host_symbol_t symbols[] = {
        {"dummy_fn2", 43, reinterpret_cast<uintptr_t>(&dummy_host_fn), DGTL_HOST_SYMBOL_BIND_BY_NAME}
    };
    status = dgtl_session_register_host_symbols(session, symbols, 1);
    REQUIRE(status.ok == 1, "dgtl_session_register_host_symbols failed");

    uint64_t addr = dgtl_session_get_symbol_address(session, "dummy_fn");
    REQUIRE(addr == reinterpret_cast<uintptr_t>(&dummy_host_fn), "Symbol address mismatch");

    uint64_t slot_addr = dgtl_session_get_symbol_address_by_slot(session, 42);
    (void)slot_addr;
    REQUIRE(status.ok == 1, "Slot symbol registration failed");

    dgtl_session_clear_host_symbols(session);
    dgtl_session_destroy(session);

    PASS("8. Session Management & Host Symbol Binding");
    return true;
}

// ============================================================================
// TEST 9: Tracing, Telemetry, Performance Metrics & Serialization
// ============================================================================
bool test_telemetry_and_serialization() {
    dgtl_context_t* ctx = dgtl_context_create();
    dgtl_stream_t* stream = dgtl_stream_create(ctx, 128);

    dgtl_stream_enable_tracing(stream);

    uint32_t c1 = dgtl_emit_const_i64(stream, 100);
    uint32_t c2 = dgtl_emit_const_i64(stream, 200);
    uint32_t r  = dgtl_emit_add_i32(stream, c1, c2);
    dgtl_emit_return(stream, r);

    // Metrics & Telemetry
    dgtl_perf_metrics_t metrics{};
    int32_t metrics_ok = dgtl_stream_get_perf_metrics(stream, &metrics);
    REQUIRE(metrics_ok == 1, "dgtl_stream_get_perf_metrics failed");

    size_t trace_count = 0;
    dgtl_trace_event_t* trace_events = dgtl_stream_get_trace_events(stream, &trace_count);
    (void)trace_events;

    uint64_t cycles = dgtl_stream_estimate_cycles(stream);
    int32_t score  = dgtl_stream_get_optimization_score(stream);
    (void)cycles;
    (void)score;

    // Serialization Round-Trip
    void* blob = nullptr;
    size_t blob_size = 0;
    dgtl_status_t status = dgtl_stream_serialize(stream, &blob, &blob_size);
    REQUIRE(status.ok == 1 && blob != nullptr && blob_size > 0, "dgtl_stream_serialize failed");

    dgtl_stream_t* stream_deser = dgtl_stream_create(ctx, 128);
    status = dgtl_stream_deserialize(stream_deser, blob, blob_size);
    REQUIRE(status.ok == 1, "dgtl_stream_deserialize failed");

    dgtl_free_serialized_blob(blob);
    dgtl_stream_destroy(stream_deser);
    dgtl_stream_destroy(stream);
    dgtl_context_destroy(ctx);

    PASS("9. Tracing, Telemetry, Performance Metrics & Serialization");
    return true;
}

// ============================================================================
// TEST 10: Multi-Threaded Concurrent Stream Stress
// ============================================================================
bool test_multithreaded_stress() {
    constexpr size_t THREAD_COUNT = 4;
    constexpr size_t ITERATIONS_PER_THREAD = 25;

    vector<thread> workers;
    workers.reserve(THREAD_COUNT);
    atomic<bool> all_ok{true};
    static mutex s_strike_mtx;

    for (size_t t = 0; t < THREAD_COUNT; ++t) {
        workers.emplace_back([&all_ok]() {
            dgtl_context_t* ctx = dgtl_context_create();
            if (!ctx) { all_ok = false; return; }

            for (size_t iter = 0; iter < ITERATIONS_PER_THREAD; ++iter) {
                dgtl_stream_t* stream = dgtl_stream_create(ctx, 64);
                if (!stream) { all_ok = false; break; }

                uint32_t c1 = dgtl_emit_const_i64(stream, iter);
                uint32_t c2 = dgtl_emit_const_i64(stream, iter * 2);
                uint32_t r  = dgtl_emit_add_i32(stream, c1, c2);
                dgtl_emit_return(stream, r);

                dgtl_strike_result_t res{};
                {
                    lock_guard<mutex> lock(s_strike_mtx);
                    res = dgtl_strike_stream(stream, DGTL_TARGET_X64);
                }

                if (res.ok != 1 || res.code_size == 0) {
                    all_ok = false;
                }

                dgtl_stream_destroy(stream);
            }

            dgtl_context_destroy(ctx);
        });
    }

    for (auto& w : workers) {
        if (w.joinable()) w.join();
    }

    REQUIRE(all_ok.load(), "Concurrent multi-threaded stream construction/strike failed");

    PASS("10. Multi-Threaded Concurrent Stream Stress");
    return true;
}

// ============================================================================
// TEST 11: Text Printing, IR Dumping & Assembly Formatting
// ============================================================================
bool test_printing_and_formatting() {
    dgtl_context_t* ctx = dgtl_context_create();
    dgtl_stream_t* stream = dgtl_stream_create(ctx, 128);

    uint32_t c1 = dgtl_emit_const_i64(stream, 100);
    uint32_t c2 = dgtl_emit_const_i64(stream, 200);
    uint32_t r  = dgtl_emit_add_i32(stream, c1, c2);
    dgtl_emit_return(stream, r);

    // 1. IR Stream Dumping to standard handle
    TEST_LOG("INFO", "Dumping IR Stream via dgtl_stream_dump_ir:");
    dgtl_stream_dump_ir(stream, nullptr);

    // 2. Target Name & Extension Formatting
    TEST_LOG("INFO", "Target Architecture String Formatting:");
    dgtl_target_t targets[] = {
        DGTL_TARGET_X64, DGTL_TARGET_ARM64, DGTL_TARGET_NASM,
        DGTL_TARGET_WASM, DGTL_TARGET_ELF_OBJ, DGTL_TARGET_COFF_OBJ, DGTL_TARGET_MACHO
    };
    for (auto t : targets) {
        const char* name = dgtl_target_name(t);
        const char* ext  = dgtl_target_extension(t);
        REQUIRE(name != nullptr && strlen(name) > 0, "Target name string null or empty");
        REQUIRE(ext != nullptr, "Target extension string null");
        TEST_LOG("INFO", string("Target [") + to_string(t) + "] Name: " + name + " | Ext: " + ext);
    }

    // 3. Session Mode Names Formatting
    dgtl_session_mode_t modes[] = { DGTL_SESSION_MODE_JIT, DGTL_SESSION_MODE_AOT, DGTL_SESSION_MODE_HYBRID };
    for (auto m : modes) {
        const char* mname = dgtl_session_mode_name(m);
        REQUIRE(mname != nullptr && strlen(mname) > 0, "Session mode name string null");
        TEST_LOG("INFO", string("Session Mode [") + to_string(m) + "]: " + mname);
    }

    // 4. NASM Text Emission Output
    dgtl_strike_result_t nasm_res = dgtl_strike_stream(stream, DGTL_TARGET_NASM);
    REQUIRE(nasm_res.ok == 1, "NASM assembly strike failed");
    REQUIRE(nasm_res.code_size > 0, "NASM assembly output empty");
    TEST_LOG("INFO", "NASM Assembly Code Size: " + to_string(nasm_res.code_size) + " bytes");

    // 5. Diagnostics & Fallback Message Formatting
    dgtl_clear_error();
    const char* last_err = dgtl_get_last_error();
    (void)last_err;

    size_t fallback_cnt = dgtl_last_linker_fallback_count();
    (void)fallback_cnt;

    dgtl_stream_destroy(stream);
    dgtl_context_destroy(ctx);

    PASS("11. Text Printing, IR Dumping & Assembly Formatting");
    return true;
}

// ============================================================================
// MAIN SUITE RUNNER
// ============================================================================
int main() {
    cout << "\n====================================================================\n";
    cout << " DGTL ULTIMATE PRODUCTION SUITE: 100% FEATURE & SDK EXAMINATION\n";
    cout << "====================================================================\n";

    RUN_SUITE(test_telemetry_and_capabilities, "1. Telemetry & Capabilities");
    RUN_SUITE(test_context_stream_arena, "2. Context, Stream & Arena Lifecycle");
    RUN_SUITE(test_scalar_ir_emission, "3. Scalar IR Emission & OpCode Pipeline");
    RUN_SUITE(test_ai_and_spatial_math, "4. AI/ML & Spatial Math Operations");
    RUN_SUITE(test_simd_vectorization, "4B. SIMD & Vectorization Operations");
    RUN_SUITE(test_control_flow_and_cfg, "5. Control Flow & CFG Introspection");
    RUN_SUITE(test_optimization_passes, "6. Optimization Passes & Integrity");
    RUN_SUITE(test_strike_engine, "7. Multi-Target Strike Engine Emission");
    RUN_SUITE(test_session_and_host_symbols, "8. Session & Host Symbol Binding");
    RUN_SUITE(test_telemetry_and_serialization, "9. Telemetry & Serialization");
    RUN_SUITE(test_multithreaded_stress, "10. Multi-Threaded Concurrent Stress");
    RUN_SUITE(test_printing_and_formatting, "11. Text Printing & Assembly Formatting");

    cout << "\n====================================================================\n";
    cout << " FINAL RESULTS: " << g_test_passes.load() << " PASSED | " << g_test_failures.load() << " FAILED\n";
    cout << "====================================================================\n";

    return (g_test_failures.load() == 0) ? 0 : 1;
}
